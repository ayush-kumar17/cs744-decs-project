#include "../include/httplib.h"
#include "cache.hpp"
#include "db.hpp"

#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <condition_variable>
#include <future>
#include <iostream>
#include <optional>
#include <memory>

using namespace std;

class ThreadPool {
public:
    explicit ThreadPool(size_t n) : stop_(false) {
        for (size_t i = 0; i < n; ++i) {
            workers_.emplace_back([this] {
                for (;;) {
                    function<void()> task;
                    {
                        unique_lock<mutex> lk(mu_);
                        cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) return;
                        task = move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            lock_guard<mutex> lg(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto &t : workers_) t.join();
    }

    void submit(function<void()> f) {
        {
            lock_guard<mutex> lg(mu_);
            tasks_.push(move(f));
        }
        cv_.notify_one();
    }

private:
    vector<thread> workers_;
    queue<function<void()>> tasks_;
    mutex mu_;
    condition_variable cv_;
    bool stop_;
};

int main(int argc, char** argv) {
    int port = 8080;
    if (argc >= 2) port = stoi(argv[1]);

    string conninfo = (argc >= 3)
        ? argv[2]
        : "host=127.0.0.1 port=5432 dbname=kvstore user=kvuser password=kvpass";

    size_t cache_capacity = 2000;
    int pool_size = 8;

    auto cache = make_shared<LRUCache>(cache_capacity);

    // ✅ Correct object construction
    auto pool = make_shared<PgPool>(conninfo, pool_size);
    auto db = make_shared<PgDB>(pool);

    ThreadPool tpool(pool_size);

    httplib::Server svr;

    // PUT /kv/<key>
    svr.Put(R"(/kv/(.*))", [db, cache, &tpool](const httplib::Request &req, httplib::Response &res) {
        string key = req.matches[1];
        string val = req.body;

        auto p = make_shared<promise<bool>>();
        auto f = p->get_future();

        tpool.submit([db, cache, key, val, p] {
            bool ok = db->put(key, val);
            if (ok) cache->put(key, val);
            p->set_value(ok);
        });

        res.status = f.get() ? 201 : 500;
    });

    // GET /kv/<key>
    svr.Get(R"(/kv/(.*))", [db, cache, &tpool](const httplib::Request &req, httplib::Response &res) {
        string key = req.matches[1];
        string v;

        if (cache->get(key, v)) {
            res.set_content(v, "text/plain");
            res.status = 200;
            return;
        }

        auto p = make_shared<promise<optional<string>>>();
        auto f = p->get_future();

        tpool.submit([db, key, p] {
            p->set_value(db->get(key));
        });

        auto val = f.get();
        if (val) {
            cache->put(key, *val);
            res.set_content(*val, "text/plain");
            res.status = 200;
        } else {
            res.status = 404;
        }
    });

    // DELETE /kv/<key>
    svr.Delete(R"(/kv/(.*))", [db, cache, &tpool](const httplib::Request &req, httplib::Response &res) {
        string key = req.matches[1];

        auto p = make_shared<promise<bool>>();
        auto f = p->get_future();

        tpool.submit([db, cache, key, p] {
            bool ok = db->del(key);
            cache->erase(key);
            p->set_value(ok);
        });

        res.status = f.get() ? 200 : 500;
    });

    cout << "KV server running on port " << port << endl;
    svr.listen("0.0.0.0", port);

    return 0;
}

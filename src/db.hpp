#pragma once
#include <libpq-fe.h>
#include <optional>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <stdexcept>

using std::string;
using std::optional;

class PgPool {
public:
    PgPool(const string &conninfo, size_t pool_size = 8) {
        for (size_t i = 0; i < pool_size; i++) {
            PGconn* c = PQconnectdb(conninfo.c_str());
            if (PQstatus(c) != CONNECTION_OK) {
                string err = PQerrorMessage(c);
                PQfinish(c);
                throw std::runtime_error("PG connect failed: " + err);
            }
            conns_.push_back(c);
            free_.push(c);
        }

        // Ensure table exists (use one connection)
        PGresult *r = PQexec(conns_[0],
            "CREATE TABLE IF NOT EXISTS kv (k TEXT PRIMARY KEY, v TEXT)");
        if (r) PQclear(r);
    }

    ~PgPool() {
        for (auto c : conns_) PQfinish(c);
    }

    PGconn* acquire() {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&]{ return !free_.empty(); });
        PGconn* c = free_.front();
        free_.pop();
        return c;
    }

    void release(PGconn* c) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            free_.push(c);
        }
        cv_.notify_one();
    }

private:
    std::vector<PGconn*> conns_;
    std::queue<PGconn*> free_;
    std::mutex mu_;
    std::condition_variable cv_;
};


class PgDB {
public:
    explicit PgDB(std::shared_ptr<PgPool> pool) : pool_(pool) {}

    bool put(const string &k, const string &v) {
        PGconn* c = pool_->acquire();
        const char *paramValues[2] = { k.c_str(), v.c_str() };
        PGresult *res = PQexecParams(
            c,
            "INSERT INTO kv (k,v) VALUES ($1,$2) "
            "ON CONFLICT (k) DO UPDATE SET v = EXCLUDED.v",
            2, nullptr, paramValues, nullptr, nullptr, 0);
        ExecStatusType st = res ? PQresultStatus(res) : PGRES_FATAL_ERROR;
        if (res) PQclear(res);
        pool_->release(c);
        return st == PGRES_COMMAND_OK;
    }

    optional<string> get(const string &k) {
        PGconn* c = pool_->acquire();
        const char *paramValues[1] = { k.c_str() };
        PGresult *res = PQexecParams(
            c,
            "SELECT v FROM kv WHERE k=$1 LIMIT 1",
            1, nullptr, paramValues, nullptr, nullptr, 0);
        ExecStatusType st = res ? PQresultStatus(res) : PGRES_FATAL_ERROR;
        if (st != PGRES_TUPLES_OK) { if (res) PQclear(res); pool_->release(c); return {}; }
        if (PQntuples(res) < 1) { PQclear(res); pool_->release(c); return {}; }
        string v = PQgetvalue(res, 0, 0);
        PQclear(res);
        pool_->release(c);
        return v;
    }

    bool del(const string &k) {
        PGconn* c = pool_->acquire();
        const char *paramValues[1] = { k.c_str() };
        PGresult *res = PQexecParams(
            c,
            "DELETE FROM kv WHERE k=$1",
            1, nullptr, paramValues, nullptr, nullptr, 0);
        ExecStatusType st = res ? PQresultStatus(res) : PGRES_FATAL_ERROR;
        if (res) PQclear(res);
        pool_->release(c);
        return (st == PGRES_COMMAND_OK);
    }

private:
    std::shared_ptr<PgPool> pool_;
};

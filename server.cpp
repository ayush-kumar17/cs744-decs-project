#include "./include/httplib.h"
#include <hiredis/hiredis.h>
#include <libpq-fe.h>
#include <iostream>
#include <chrono>
#include <mutex>

using namespace std;

mutex redis_mutex;
mutex pg_mutex;

int main() {
    redisContext* redis = redisConnect("127.0.0.1", 6379);
    if (!redis || redis->err) {
        cerr << "Redis connection failed" << endl;
        return 1;
    }
    cout << "Connected to Redis" << endl;

    PGconn* pg = PQconnectdb("host=127.0.0.1 dbname=kvstore user=kvuser password=kvpass");
    if (PQstatus(pg) != CONNECTION_OK) {
        cerr << "PostgreSQL connection failed" << endl;
        return 1;
    }
    cout << "Connected to PostgreSQL" << endl;

    httplib::Server svr;

    svr.Put(R"(/kv/(.*))", [&](const httplib::Request& req, httplib::Response& res) {
        auto start = chrono::high_resolution_clock::now();
        string key = req.matches[1];
        string val = req.body;
        cout << "[REQ] PUT key=" << key << endl;

        {
            lock_guard<mutex> lock(pg_mutex);
            const char* params[2] = { key.c_str(), val.c_str() };
            PGresult* r = PQexecParams(pg,
                "INSERT INTO kv (k, v) VALUES ($1, $2) ON CONFLICT (k) DO UPDATE SET v = EXCLUDED.v",
                2, NULL, params, NULL, NULL, 0);
            if (PQresultStatus(r) != PGRES_COMMAND_OK) {
                PQclear(r);
                res.status = 500;
                return;
            }
            PQclear(r);
        }

        {
            lock_guard<mutex> lock(redis_mutex);
            redisReply* r = (redisReply*)redisCommand(redis, "SET %s %s", key.c_str(), val.c_str());
            if (r) freeReplyObject(r);
        }

        cout << "[WRITE] Stored key=" << key << " in DB and Cache" << endl;
        auto end = chrono::high_resolution_clock::now();
        cout << "[TIME] " << chrono::duration<double, micro>(end - start).count() << " us" << endl;
        res.status = 201;
    });

    svr.Get(R"(/kv/(.*))", [&](const httplib::Request& req, httplib::Response& res) {
        auto start = chrono::high_resolution_clock::now();
        string key = req.matches[1];
        cout << "[REQ] GET key=" << key << endl;

        redisReply* reply = nullptr;
        {
            lock_guard<mutex> lock(redis_mutex);
            reply = (redisReply*)redisCommand(redis, "GET %s", key.c_str());
        }

        if (reply && reply->type == REDIS_REPLY_STRING) {
            cout << "[CACHE HIT] key=" << key << endl;
            res.set_content(reply->str, "text/plain");
            res.status = 200;
            freeReplyObject(reply);
            auto end = chrono::high_resolution_clock::now();
            cout << "[TIME] " << chrono::duration<double, micro>(end - start).count() << " us\n";
            return;
        }

        if (reply) freeReplyObject(reply);
        cout << "[CACHE MISS] key=" << key << endl;

        string val;
        {
            lock_guard<mutex> lock(pg_mutex);
            const char* params[1] = { key.c_str() };
            PGresult* r = PQexecParams(pg, "SELECT v FROM kv WHERE k=$1",
                                       1, NULL, params, NULL, NULL, 0);
            if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) == 0) {
                cout << "[DB MISS] key=" << key << endl;
                PQclear(r);
                res.status = 404;
                auto end = chrono::high_resolution_clock::now();
                cout << "[TIME] " << chrono::duration<double, micro>(end - start).count() << " us\n";
                return;
            }
            val = PQgetvalue(r, 0, 0);
            PQclear(r);
        }

        {
            lock_guard<mutex> lock(redis_mutex);
            redisReply* r = (redisReply*)redisCommand(redis, "SET %s %s", key.c_str(), val.c_str());
            if (r) freeReplyObject(r);
        }

        cout << "[DB HIT] key=" << key << " loaded into cache" << endl;
        res.set_content(val, "text/plain");
        res.status = 200;
        auto end = chrono::high_resolution_clock::now();
        cout << "[TIME] " << chrono::duration<double, micro>(end - start).count() << " us\n";
    });

    svr.Delete(R"(/kv/(.*))", [&](const httplib::Request& req, httplib::Response& res) {
        auto start = chrono::high_resolution_clock::now();
        string key = req.matches[1];
        cout << "[REQ] DELETE key=" << key << endl;

        {
            lock_guard<mutex> lock(pg_mutex);
            const char* params[1] = { key.c_str() };
            PGresult* r = PQexecParams(pg, "DELETE FROM kv WHERE k=$1", 1, NULL, params, NULL, NULL, 0);
            PQclear(r);
        }

        {
            lock_guard<mutex> lock(redis_mutex);
            redisReply* r = (redisReply*)redisCommand(redis, "DEL %s", key.c_str());
            if (r) freeReplyObject(r);
        }

        cout << "[DELETE] key=" << key << " removed from DB and Cache" << endl;
        auto end = chrono::high_resolution_clock::now();
        cout << "[TIME] " << chrono::duration<double, micro>(end - start).count() << " us\n";
        res.status = 200;
    });

    // ✅ NEW ROUTE ADDED HERE
    svr.Get("/check_cache", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("key")) {
            res.status = 400;
            res.set_content("Missing key\n", "text/plain");
            return;
        }

        string key = req.get_param_value("key");
        redisReply* reply = nullptr;
        {
            lock_guard<mutex> lock(redis_mutex);
            reply = (redisReply*)redisCommand(redis, "EXISTS %s", key.c_str());
        }

        if (!reply) {
            res.status = 500;
            res.set_content("Redis error\n", "text/plain");
            return;
        }

        if (reply->integer > 0) {
            res.set_content("Key exists in cache\n", "text/plain");
        } else {
            res.status = 404;
            res.set_content("Key not in cache\n", "text/plain");
        }

        freeReplyObject(reply);
    });
    // ✅ END NEW ROUTE

    cout << "Server running on http://localhost:8080" << endl;
    svr.listen("0.0.0.0", 8080);

    PQfinish(pg);
    redisFree(redis);
    return 0;
}

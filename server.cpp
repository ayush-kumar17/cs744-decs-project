#include "./include/httplib.h"
#include <hiredis/hiredis.h>
#include <libpq-fe.h>
#include <iostream>
#include <chrono>

using namespace std;

int main() {
    // Connect to Redis
    redisContext* redis = redisConnect("127.0.0.1", 6379);
    if (!redis || redis->err) {
        cerr << "Redis connection failed" << endl;
        return 1;
    }
    cout << "Connected to Redis" << endl;

    // Connect to PostgreSQL
    PGconn* pg = PQconnectdb("host=127.0.0.1 dbname=kvstore user=kvuser password=kvpass");
    if (PQstatus(pg) != CONNECTION_OK) {
        cerr << "PostgreSQL connection failed" << endl;
        return 1;
    }
    cout << "Connected to PostgreSQL" << endl;

    httplib::Server svr;

    // PUT /kv/<key>
    svr.Put(R"(/kv/(.*))", [&](const httplib::Request& req, httplib::Response& res) {
        auto start = chrono::high_resolution_clock::now();
        string key = req.matches[1];
        string val = req.body;

        cout << "[REQ] PUT key=" << key << endl;

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

        redisCommand(redis, "SET %s %s", key.c_str(), val.c_str());
        cout << "[WRITE] Stored key=" << key << " in DB and Cache" << endl;

        auto end = chrono::high_resolution_clock::now();
        cout << "[TIME] " << chrono::duration<double, micro>(end - start).count() << " us" << endl;

        res.status = 201;
    });

    // GET /kv/<key>
    svr.Get(R"(/kv/(.*))", [&](const httplib::Request& req, httplib::Response& res) {
        auto start = chrono::high_resolution_clock::now();
        string key = req.matches[1];

        cout << "[REQ] GET key=" << key << endl;

        redisReply* reply = (redisReply*)redisCommand(redis, "GET %s", key.c_str());
        if (reply && reply->type == REDIS_REPLY_STRING) {
            cout << "[CACHE HIT] key=" << key << endl;
            res.set_content(reply->str, "text/plain");
            res.status = 200;
            freeReplyObject(reply);

            auto end = chrono::high_resolution_clock::now();
            cout << "[TIME] " << chrono::duration<double, micro>(end - start).count() << " us\n";
            return;
        }
        freeReplyObject(reply);
        cout << "[CACHE MISS] key=" << key << endl;

        const char* params[1] = { key.c_str() };
        PGresult* r = PQexecParams(pg, "SELECT v FROM kv WHERE k=$1",
                                   1, NULL, params, NULL, NULL, 0);

        if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) == 0) {
            cout << "[DB MISS] key=" << key << " not found" << endl;
            PQclear(r);
            res.status = 404;

            auto end = chrono::high_resolution_clock::now();
            cout << "[TIME] " << chrono::duration<double, micro>(end - start).count() << " us\n";
            return;
        }

        string val = PQgetvalue(r, 0, 0);
        PQclear(r);

        redisCommand(redis, "SET %s %s", key.c_str(), val.c_str());
        cout << "[DB HIT] key=" << key << " loaded into cache" << endl;

        res.set_content(val, "text/plain");
        res.status = 200;

        auto end = chrono::high_resolution_clock::now();
        cout << "[TIME] " << chrono::duration<double, micro>(end - start).count() << " us\n";
    });

    // DELETE /kv/<key>
    svr.Delete(R"(/kv/(.*))", [&](const httplib::Request& req, httplib::Response& res) {
        auto start = chrono::high_resolution_clock::now();
        string key = req.matches[1];

        cout << "[REQ] DELETE key=" << key << endl;

        const char* params[1] = { key.c_str() };
        PGresult* r = PQexecParams(pg, "DELETE FROM kv WHERE k=$1", 1, NULL, params, NULL, NULL, 0);
        PQclear(r);

        redisCommand(redis, "DEL %s", key.c_str());
        cout << "[DELETE] key=" << key << " removed from DB and Cache" << endl;

        auto end = chrono::high_resolution_clock::now();
        cout << "[TIME] " << chrono::duration<double, micro>(end - start).count() << " us\n";

        res.status = 200;
    });

    cout << "Server running on http://localhost:8080" << endl;
    svr.listen("0.0.0.0", 8080);

    PQfinish(pg);
    redisFree(redis);
    return 0;
}

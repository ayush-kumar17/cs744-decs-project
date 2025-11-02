#pragma once
#include <list>
#include <unordered_map>
#include <string>
#include <mutex>

using std::string;
using std::list;
using std::pair;
using std::unordered_map;
using std::mutex;
using std::lock_guard;

class LRUCache {
public:
    explicit LRUCache(size_t capacity = 1000) : cap_(capacity) {}

    bool get(const string &k, string &v) {
        lock_guard<mutex> lg(mu_);
        auto it = mp_.find(k);
        if (it == mp_.end()) return false;
        // move node to front
        pages_.splice(pages_.begin(), pages_, it->second);
        v = it->second->second;
        return true;
    }

    void put(const string &k, const string &v) {
        lock_guard<mutex> lg(mu_);
        auto it = mp_.find(k);
        if (it != mp_.end()) {
            it->second->second = v;
            pages_.splice(pages_.begin(), pages_, it->second);
            return;
        }
        if (pages_.size() >= cap_) {
            auto &last = pages_.back();
            mp_.erase(last.first);
            pages_.pop_back();
        }
        pages_.emplace_front(k, v);
        mp_[k] = pages_.begin();
    }

    void erase(const string &k) {
        lock_guard<mutex> lg(mu_);
        auto it = mp_.find(k);
        if (it == mp_.end()) return;
        pages_.erase(it->second);
        mp_.erase(it);
    }

private:
    size_t cap_;
    list<pair<string,string>> pages_;
    unordered_map<string, decltype(pages_.begin())> mp_;
    mutable mutex mu_;
};

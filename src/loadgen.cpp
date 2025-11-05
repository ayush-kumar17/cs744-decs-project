// loadgen.cpp
#include <curl/curl.h>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <iostream>
#include <random>

struct Stats {
    std::atomic<int> success{0};
    std::atomic<int> fail{0};
    std::atomic<long long> total_ns{0};
};

size_t write_cb(void* ptr, size_t size, size_t nmemb, void* userdata){
    return size * nmemb; // discard body
}

void worker(const std::string& base, const std::string& mode, int id, int duration_s, Stats &st, int popular_k) {
    CURL *curl = curl_easy_init();
    if (!curl) return;

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); // prevents multithread hang

    std::mt19937_64 rng(std::random_device{}() + id);
    std::uniform_int_distribution<int> d(0, 1000000);
    std::uniform_int_distribution<int> pop(0, std::max(1, popular_k)-1);

    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(duration_s);
    while (std::chrono::steady_clock::now() < end) {

        // ----- Generate Request -----
        std::string url, body;
        bool is_put = false;

        if (mode == "put-all") {
            std::string k = "k_" + std::to_string(d(rng));
            url = base + "/kv/" + k;
            body = "v_" + std::to_string(d(rng));
            is_put = true;
        }
        else if (mode == "get-all") {
            std::string k = "k_" + std::to_string(d(rng));
            url = base + "/kv/" + k;
        }
        else if (mode == "get-popular") {
            std::string k = "popular_" + std::to_string(pop(rng));
            url = base + "/kv/" + k;
        }
        else { // mixed
            if (d(rng) % 3 == 0) {
                std::string k = "k_" + std::to_string(d(rng));
                url = base + "/kv/" + k;
                body = "v_" + std::to_string(d(rng));
                is_put = true;
            } else {
                std::string k = "k_" + std::to_string(d(rng));
                url = base + "/kv/" + k;
            }
        }

        // ----- Set Request -----
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        if (is_put) {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 0L);
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        } else {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        }

        // ----- Send -----
        auto t0 = std::chrono::steady_clock::now();
        CURLcode rc = curl_easy_perform(curl);
        auto t1 = std::chrono::steady_clock::now();

        long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        st.total_ns += ns;

        if (rc == CURLE_OK) st.success++;
        else st.fail++;
    }

    curl_easy_cleanup(curl);
}

int main(int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "Usage: loadgen <server-base-url> <mode> <num-threads> <duration-s> <popular-k>\n";
        std::cerr << "Modes: put-all, get-all, get-popular, mixed\n";
        return 1;
    }

    std::string base = argv[1];
    std::string mode = argv[2];
    int nthreads = std::stoi(argv[3]);
    int duration = std::stoi(argv[4]);
    int popular_k = std::stoi(argv[5]);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    Stats st;
    std::vector<std::thread> threads;

    for (int i = 0; i < nthreads; ++i)
        threads.emplace_back(worker, base, mode, i, duration, std::ref(st), popular_k);

    for (auto &t : threads)
        t.join();

    curl_global_cleanup();

    int succ = st.success.load();
    int fail = st.fail.load();
    double avg_ms = succ ? (st.total_ns.load() / 1e6) / succ : 0.0;

    std::cout << "Success=" << succ << " Fail=" << fail << " AvgLatency(ms)=" << avg_ms << "\n";
    return 0;
}

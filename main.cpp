#include <iostream>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <functional>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <unordered_map>

// 假设这些定义在外部头文件中
enum class StatusCode {
    OK,
    SymbolNotFound,
    ClientOrderIDDuplicate,
    CancelOrderIDNotFound
};

enum class Side {
    BUY,
    SELL
};

enum class TimeInForce {
    DAY,
    IOC
};

struct PriceTimeKey {
    double price;
    std::chrono::high_resolution_clock::time_point time_point;

    PriceTimeKey(double p) : price(p), time_point(std::chrono::high_resolution_clock::now()) {}

    bool operator<(const PriceTimeKey& other) const {
        return price < other.price || (price == other.price && time_point < other.time_point);
    }
    bool operator>(const PriceTimeKey& other) const {
        return price > other.price || (price == other.price && time_point > other.time_point);
    }
};

struct Order {
    std::string session_id;
    std::string account;
    std::string client_order_id;
    double order_qty;
    double price;
    Side side;
    std::string symbol;
    TimeInForce time_in_force;
    double min_qty = 0.0;

    uint64_t order_id;
    PriceTimeKey price_time_key;

    double cum_qty;
    double leave_qty;

    double last_qty;
    double last_price;

    bool post_only = false;

    Order() : price_time_key(0.0) {}
};

using OrderPtr = std::shared_ptr<Order>;

struct CancelInfo {
    std::string symbol;
    uint64_t order_id;
};

struct MatchedPair {
    double matched_qty;
    double matched_price;
    double in_leave_qty;
    double in_cum_qty;
    double mm_leave_qty;
    double mm_cum_qty;
    int64_t trade_report_id;
    std::string exec_id;
    OrderPtr in_order;
    OrderPtr mm_order;
};

class SingleOrderBook {
public:
    SingleOrderBook() = delete;
    explicit SingleOrderBook(const std::string& symbol) : symbol_(symbol) {}

    StatusCode processOrder(OrderPtr order) {
        std::cout << "Processing order for symbol: " << order->symbol << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // 模拟处理时间
        std::cout << "Order processed: " << order->symbol << ", Price: " << order->price << ", Qty: " << order->order_qty << std::endl;
        return StatusCode::OK;
    }

    StatusCode justAdd(OrderPtr mm_order) {
        return StatusCode::OK;
    }

    StatusCode cancel(const CancelInfo& cancel_info) {
        return StatusCode::OK;
    }

private:
    std::string symbol_;
};

class OrderBook {
public:
    // 获取单例实例
    static OrderBook& instance() {
        static OrderBook instance;
        return instance;
    }

    // 静态初始化函数，用于注册符号和对应的订单簿
    static void initialize(const std::vector<std::vector<std::string>>& symbol_groups) {
        OrderBook& instance = OrderBook::instance();
        instance.num_threads_ = symbol_groups.size();

        // 将symbol_groups转换为symbol_to_thread映射
        for (size_t i = 0; i < symbol_groups.size(); ++i) {
            for (const auto& symbol : symbol_groups[i]) {
                instance.symbol_to_thread_[symbol] = i;
                instance.order_books_.emplace(symbol, std::make_unique<SingleOrderBook>(symbol));
            }
        }

        // 初始化线程和队列
        for (size_t i = 0; i < instance.num_threads_; ++i) {
            instance.queues_.emplace_back(std::queue<OrderPtr>());
            instance.threads_.emplace_back(&OrderBook::workerThread, &instance, i);
        }
    }

    // 析构函数
    ~OrderBook() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
            condition_.notify_all();
        }

        for (std::thread& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        order_books_.clear();
    }

    // 处理订单
    void processOrder(OrderPtr order) {
        size_t thread_index = getThreadIndex(order->symbol);
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queues_[thread_index].push(order);
        }
        condition_.notify_one();
    }

    // 添加订单
    StatusCode justAdd(OrderPtr mm_order) {
        auto it = order_books_.find(mm_order->symbol);
        if (it != order_books_.end()) {
            return it->second->justAdd(std::move(mm_order));
        }
        return StatusCode::SymbolNotFound;
    }

    // 取消订单
    StatusCode cancel(const CancelInfo& cancel_info) {
        auto it = order_books_.find(cancel_info.symbol);
        if (it != order_books_.end()) {
            return it->second->cancel(cancel_info);
        }
        return StatusCode::SymbolNotFound;
    }

private:
    // 私有构造函数
    OrderBook() : stop_(false), num_threads_(0) {}

    // 工作线程函数
    void workerThread(size_t index) {
        while (true) {
            OrderPtr order;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this, index] { return stop_ || !queues_[index].empty(); });
                if (stop_ && queues_[index].empty()) {
                    return;
                }
                order = queues_[index].front();
                queues_[index].pop();
            }
            auto it = order_books_.find(order->symbol);
            if (it != order_books_.end()) {
                it->second->processOrder(std::move(order));
            }
        }
    }

    // 获取线程索引
    size_t getThreadIndex(const std::string& symbol) const {
        auto it = symbol_to_thread_.find(symbol);
        if (it != symbol_to_thread_.end()) {
            return it->second;
        }
        else {
            throw std::runtime_error("Symbol not found in symbol_to_thread mapping");
        }
    }

    std::unordered_map<std::string, std::unique_ptr<SingleOrderBook>> order_books_;
    std::vector<std::queue<OrderPtr>> queues_;
    std::vector<std::thread> threads_;
    std::unordered_map<std::string, size_t> symbol_to_thread_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
    size_t num_threads_;
};

int main() {
    // 初始化符号分组
    std::vector<std::vector<std::string>> symbol_groups = {
            {"AAPL", "MSFT"},  // 分配给线程0
            {"GOOG"}           // 分配给线程1
            // 其他线程可以根据需要继续添加
    };

    OrderBook::initialize(symbol_groups);  // 使用symbol_groups.size()个线程

    OrderBook& order_book = OrderBook::instance();

    // 创建订单示例
    OrderPtr order1 = std::make_shared<Order>();
    order1->symbol = "AAPL";  // 设置订单符号
    order1->price = 150.0;
    order1->order_qty = 10;

    OrderPtr order2 = std::make_shared<Order>();
    order2->symbol = "GOOG";  // 设置订单符号
    order2->price = 2500.0;
    order2->order_qty = 5;

    // 处理订单
    order_book.processOrder(order1);
    order_book.processOrder(order2);

    // 模拟一段时间的处理
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 单例对象销毁时自动清理资源

    return 0;
}
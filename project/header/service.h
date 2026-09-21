#ifndef HEADER_SER_H
#define HEADER_SER_H
#include <boost/asio/io_context.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <deque>
#include <functional>
#include <iostream>
#include <boost/asio.hpp>
#include <memory>
#include <set>
namespace asio = boost::asio;
using tcp = asio::ip::tcp;
class Session;
struct User{
    std::string name; //名字
    int userId_;    // 用户id   
    boost::shared_ptr<Session> clients_; //指向客户端的共享指针
};

struct Conservation{
    int userID_;    //群聊标识 
    std::set<User> clients_;  //群聊中的用户
};
class Session:std::enable_shared_from_this<Session>{
private:
    tcp::socket socket_;    //持有的套接字
    asio::streambuf buffer_;    //容器
    std::deque<std::shared_ptr<std::string>>writeQueue_;    //写队列
    std::string name_;  //名字
    bool running_ = false;  //运行的状态
private:
    //向群聊发送消息
    /*参数 msg 要广播的消息 功能 将消息广播给群聊中的所有客户端 */
    void BroadCast(const std::string & msg);

    /*参数 无 功能 异步读取对端发来的数据 */
    void doRead();

    /*参数 无 功能 将写队列中的数据异步发送出去 */
    void doWrite();


public:
    using sessionPtr = std::shared_ptr<Session>;
    using handleMsg = std::function<void(const sessionPtr & Session,const std::string & msg)>;
    using closeSession = std::function<void(const sessionPtr& Session)>;
    /*参数 socket_ 连接的套接字 handleFunc_ 消息处理回调 功能 构造会话并开始读写 */
    Session(tcp::socket socket_, handleMsg handleFunc_);

};

class ChatService{
public:    using sessionPtr = std::shared_ptr<Session>;
private:
    asio::io_context io_context_; //异步任务的总调度
    tcp::acceptor acceptor_; //接收器
    tcp::socket socket_; //用于通信的套接字
    //服务器是否运行
    bool isRunning_;
    //在线的用户
    std::set<User> clients_; 
    //群聊
    std::set<Conservation> Conservations;
private:
    //接受连接
    /*参数 无 功能 异步等待并接受新的客户端连接 */
    void doAccept();    //接受连接
    //包处理函数 参数1 发包的session 参数2
    /*参数 Session 发包的会话指针 msg 收到的消息内容 功能 处理客户端发来的包 */
    void handleMessage(const sessionPtr & Session,const std::string & msg);
    /*参数 Session 要关闭的会话指针 功能 关闭并移除指定会话 */
    void closeSession(const sessionPtr & Session);
public:
    /*参数 io 异步调度上下文 port 监听端口 功能 初始化服务并开始监听 */
    ChatService(asio::io_context io,size_t port);
};


#endif // HEADER_SER_H
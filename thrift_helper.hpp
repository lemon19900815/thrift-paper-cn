#ifndef __THRIFT_HELPER_H__
#define __THRIFT_HELPER_H__

#include <functional>
#include <string>
#include <cstdint>
#include <mutex> // std::once_flag
#include <thread>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/protocol/TJSONProtocol.h>
#include <thrift/protocol/TCompactProtocol.h>

#include <thrift/server/TSimpleServer.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/server/TThreadPoolServer.h>
#include <thrift/server/TNonblockingServer.h>

#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/THttpServer.h>
#include <thrift/transport/TNonblockingServerSocket.h>

#include <thrift/transport/THttpClient.h>
#include <thrift/processor/TMultiplexedProcessor.h>

#include <thrift/async/TAsyncBufferProcessor.h>
#include <thrift/async/TAsyncProtocolProcessor.h>
#include <thrift/async/TEvhttpServer.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

// 传输层模板(协议)
template<typename TransportFactory, typename ProtocolFactory>
struct tf_trans_traits
{
    static_assert(std::is_base_of_v<TTransportFactory, transport_factory>,
                      "TransportFactory must based on TTransportFactory!!!");
    static_assert(std::is_base_of_v<TProtocolFactory, protocol_factory>,
                      "ProtocolFactory must based on TProtocolFactory!!!");

    using transport_factory = typename TransportFactory;
    using protocol_factory = typename ProtocolFactory;

    static std::shared_ptr<TTransportFactory> create_transport_factory()
    {
        return std::make_shared<transport_factory>();
    }

    static std::shared_ptr<TProtocolFactory> create_protocol_factory()
    {
        return std::make_shared<protocol_factory>();
    }
};

// 业务层模板
template<typename Handler, typename HandlerFactory,
         typename Processor, typename ProcessorFactory>
struct tf_logic_traits
{
    using handler_type = typename Handler;
    using handler_factory = typename HandlerFactory;

    using processor_type = typename Processor;
    using processor_factory = typename ProcessorFactory;

    static std::shared_ptr<handler_type> create_handler()
    {
        return std::make_shared<handler_type>();
    }

    static std::shared_ptr<handler_factory> create_handler_factory()
    {
        return std::make_shared<handler_factory>();
    }

    static std::shared_ptr<processor_type> create_processor()
    {
        return std::make_shared<processor_type>(create_handler());
    }

    static std::shared_ptr<processor_factory> create_processor_factory()
    {
        return std::make_shared<processor_factory>(create_handler_factory());
    }
};

// logic_traits辅助定义
#define LOGIC_TRAITS(logic) \
    using logic_traits = tf_logic_traits <\
                         logic##Handler, logic##HandlerFactory,\
                         logic##Processor, logic##ProcessorFactory >

// 展开如何下所示，注意TunnelServiceHandler实现的命名规则
//using logic_traits = tf_logic_traits <
//                     TunnelServiceHandler, TunnelServiceHandlerFactory,
//                     TunnelServiceProcessor, TunnelServiceProcessorFactory >;

// 默认的传输方式和协议类型，用户可以对该默认进行修改
namespace default_tf_traits
{

// TFramedTransport – 以frame为单位进行传输，非阻塞式服务中使用（TNonblockingServer）
// TFileTransport – 将数据写入本地文件或从文件读取数据。主要用于本地文件系统的数据传输。
// TSimpleFileTransport - 以文件形式进行传输：只能append
// TBufferedTransport – 在底层传输协议之上添加缓冲区，以提高数据传输的效率。
// TZlibTransport – 使用 zlib 库对数据进行压缩和解压缩，以减小数据传输的大小
// THttpTransport - 使用http进行传输（SubClass: THttpClient,THttpServer）
using transport = THttpClient;

// TBinaryProtocol – 二进制格式.
// TCompactProtocol – 紧凑的二进制格式
// TJSONProtocol – JSON格式
// TDebugProtocol – 使用易懂的可读的文本格式，便于调试
using protocol = TJSONProtocol;

// 传输层工厂
// TFramedTransportFactory - TFramedTransport
// TBufferedTransportFactory - TBufferedTransport
// TZlibTransportFactory - TZlibTransport
// THttpServerTransportFactory - THttpServer(服务器)
using transport_factory = THttpServerTransportFactory;

// 协议层工厂
// TBinaryProtocolFactory - TBinaryProtocol
// TCompactProtocolFactory - TCompactProtocol
// TJSONProtocolFactory - TJSONProtocol
// TDebugProtocolFactory - TDebugProtocol
using protocol_factory = TJSONProtocolFactory;

// 传输层工厂&协议层工厂
using trans_traits = tf_trans_traits<transport_factory, protocol_factory>;

}

// thrift的工作模式
enum class tf_workmode
{
    TF_SIMPLE,       // TSimpleServer
    TF_THREAD,       // TThreadedServer
    TF_NOBLOCKING,   // TNonblockingServer
    TF_POOL,         // TThreadPoolServer
};

inline std::string to_string(tf_workmode mode)
{
    std::string mode_str;
    switch(mode)
    {
    case tf_workmode::TF_SIMPLE:
        mode_str = "simple";
        break;
    case tf_workmode::TF_THREAD:
        mode_str = "thread";
        break;
    case tf_workmode::TF_NOBLOCKING:
        mode_str = "noblocking";
        break;
    case tf_workmode::TF_POOL:
        mode_str = "pool";
        break;
    default:
        mode_str = "uknown";
        break;
    }
    return mode_str;
}

// 辅助thrift创建服务帮助类，使用方法
// 1、使用宏LOGIC_TRAITS定义服务的Logic类型
// 2、指定服务的传输层工厂和协议层工厂类型，如：default_tf_traits::trans_traits
// 3、创建thrift_service对象
// 4、在指定端口以某种方式工作方式开启thrift服务
// LOGIC_TRAITS(TunnelService);
// thrift_service<logic_traits> service;
// service.start(9090, tf_workmode::TF_POOL);
// service.serve();

// TODO: 添加多处理器-processor
//template<typename Logic = tf_logic_traits<>, typename Trans = tf_trans_traits<>>
template<typename Logic, typename Trans = default_tf_traits::trans_traits>
class thrift_service
{
public:
    virtual ~thrift_service()
    {
        stop();
    }
    
    void set_logger(std::function<void(std::string msg)> logger)
    {
        logger_ = std::move(logger);
    }

    void start(int32_t port, tf_workmode mode = tf_workmode::TF_POOL)
    {
        if (server_)
        {
            log("WARNING: thrift service is started.");
            return;
        }

        thd_ = std::thread([this, mode, port]()
        {
            try
            {
                create_service(mode, port);

                if (server_)
                {
                    log("thrift service on port: " + std::to_string(port));
                    server_->serve(); // 在线程中阻塞提供服务，直到退出
                }
                else
                {
                    log("ERROR: create thrift service failed on mode:" + to_string(mode));
                }
            }
            catch (TException &ex)
            {
                log("ERROR: " + ex.what());
            }
            catch (...)
            {
                log("ERROR: Unknown Error start thrift service.");
            }
        });
    }

    void stop()
    {
        std::call_once(flag_, [this]()
        {
            if (server_)
            {
                server_->stop();
                serve();

                server_ = nullptr;
            }
        });
    }

    void serve()
    {
        if (thd_.joinable())
        {
            thd_.join();
        }
    }

private:
    void create_service(tf_workmode mode, int32_t port)
    {
        switch (mode)
        {
        case tf_workmode::TF_SIMPLE:
            create_simple_service(port);
            break;
        case tf_workmode::TF_THREAD:
            create_thread_server(port);
            break;
        case tf_workmode::TF_NOBLOCKING:
            create_noblocking_server(port);
            break;
        case tf_workmode::TF_POOL:
            create_threadpool_server(port);
            break;
        default:
            log("unknown work mode.");
            break;
        }
    }

    // 单线程工作模式
    void create_simple_service(int32_t port)
    {
        // This server only allows one
        // connection at a time, but spawns no threads
        auto server_sock = std::make_shared<TServerSocket>(port);

        auto trans_factory = Trans::create_transport_factory();
        auto proto_factory = Trans::create_protocol_factory();

        server_ = std::make_unique<TSimpleServer>(
                      Logic::create_processor(),
                      server_sock, trans_factory, proto_factory);
    }

    // 多线程阻塞模式
    void create_thread_server(int32_t port, bool use_single_handler = false)
    {
        auto server_sock = std::make_shared<TServerSocket>(port);

        auto trans_factory = Trans::create_transport_factory();
        auto proto_factory = Trans::create_protocol_factory();

        if (use_single_handler)
        {
            // 注意：**确保接口是可重入的**
            // 使用同一个handler对象
            server_ = std::make_unique<TThreadedServer>(
                          Logic::create_processor(),
                          server_sock, trans_factory, proto_factory);
        }
        else
        {
            // 使用不同的handler对象
            server_ = std::make_unique<TThreadedServer>(
                          Logic::create_processor_factory(),
                          server_sock, trans_factory, proto_factory);
        }
    }

    // 非阻塞模式
    void create_noblocking_server(int32_t port)
    {
        // Thrift 的 TNonblockingServer 只支持 TFramedTransport 传输。
        // 这是因为 TNonblockingServer 使用帧（frame）作为消息的边界，
        // 而 TFramedTransport 提供了对帧传输的支持。

        // 创建传输层和协议层
        //auto trans_factory = std::make_shared<TFramedTransportFactory>();
        auto trans_factory = Trans::create_transport_factory();
        auto proto_factory = Trans::create_protocol_factory();

        // 创建非阻塞服务器模型
        auto server_sock = std::make_shared<TNonblockingServerSocket>(port);

        // 不能设置transportFactory，否则会出现下面的异常
        // TNonblockingServer: client died: Frame size has negative value

        //TNonblockingServer server(processorFactory,
        //                          transportFactory, transportFactory,
        //                          protocolFactory, protocolFactory,
        //                          serverTransport, threadManager);

        auto service = std::make_unique<TNonblockingServer>(
                           Logic::create_processor_factory(),
                           proto_factory, server_sock,
                           create_thread_manager());
        service->setNumIOThreads(io_thread_num());

        server_ = std::move(service);
    }

    // 线程池服务器模型
    void create_threadpool_server(int32_t port)
    {
        auto server_sock = std::make_shared<TServerSocket>(port);

        auto trans_factory = Trans::create_transport_factory();
        auto proto_factory = Trans::create_protocol_factory();

        // This server allows "workerCount"
        // connection at a time, and reuses threads
        server_ = std::make_unique<TThreadPoolServer>(
                      Logic::create_processor_factory(),
                      server_sock, trans_factory, proto_factory,
                      create_thread_manager());
    }

    uint32_t threads_num()
    {
        // 最少4个线程
        return std::max<uint32_t>(4, std::thread::hardware_concurrency());
    }

    uint32_t io_thread_num()
    {
        return threads_num() / 2 + 1;
    }

    std::shared_ptr<ThreadManager> create_thread_manager()
    {
        auto worker_count = threads_num() * 2;

        auto thread_mgr = ThreadManager::newSimpleThreadManager(worker_count);
        thread_mgr->threadFactory(std::make_shared<ThreadFactory>());
        thread_mgr->start();

        return thread_mgr;
    }
    
    void log(std::string msg)
    {
        if(logger_)
        {
            logger_(msg);
        }
    }

private:
    std::thread thd_;
    std::unique_ptr<TServer> server_{nullptr};
    std::function<void(std::string msg)> logger_{ nullptr };

    std::once_flag flag_; // 停止在多线程环境下只调用一次
};

// 辅助客户端调用thrift服务，使用方法：
// thrift_client<TunnelServiceClient> cli;
// cli.call("localhost", 9090, [](TunnelServiceClient& stub)
// {
//     QcParam param;
//     param.__set_body("hello");
// 
//     ResultStr ret;
//     stub.QcDownload(ret, param);
//     ret.printTo(std::cout);
// });
// TODO：设置client参数，e.g. 超时时间等
template <
    typename Stub,
    typename Transport = default_tf_traits::transport,
    typename Protocol = default_tf_traits::protocol >
class thrift_client
{
public:
    void call(std::string host, int32_t port,
              std::function<void(Stub &stub)> &&fn)
    {
        auto sock = std::make_shared<TSocket>(host, port);

        auto trans = std::make_shared<Transport>(sock);
        auto proto = std::make_shared<Protocol>(trans);

        // noblocking工作模式只能以TFramedTransport作为传输方式
        // reference thrift paper：7.9- Nonblocking Operation
        // https://thrift.apache.org/static/files/thrift-20070401.pdf
        //auto transport = std::make_shared<TFramedTransport>(sock);
        //auto protocol = std::make_shared<TJSONProtocol>(transport);
        //auto protocol = std::make_shared<TCompactProtocol>(transport);

        try
        {
            trans->open();

            Stub stub(proto);
            fn(stub);

            trans->close();
        }
        catch (TException &ex)
        {
            FLOG(ERROR) << ex.what() << std::endl;
        }
        catch (...)
        {
            FLOG(ERROR) << "Unknown ERROR" << std::endl;
        }
    }
};

// 转换器：thrift对象和string相互转换
template<typename T, typename P>
class converter
{
    static_assert(std::is_base_of_v<TBase, T>, "T must based on TBase");
    static_assert(std::is_base_of_v<TProtocol, P>, "P must based on TProtocol");

public:
    // 从字符串到thrift对象的转换
    static T to_thrift(const std::string &content)
    {
        auto buffer = std::make_shared<TMemoryBuffer>(
                          (uint8_t *)content.c_str(),
                          static_cast<uint32_t>(content.size()));

        auto proto = std::make_shared<P>(buffer);

        T obj;
        obj.read(proto.get());

        return obj;
    }

    // 从thrift对象到字符串的转换
    static std::string to_string(const T &obj)
    {
        auto buffer = std::make_shared<TMemoryBuffer>();
        auto proto = std::make_shared<P>(buffer);

        obj.write(proto.get());

        return buffer->getBufferAsString();
    }
};

// 注：json的key是field id,与常规json存在差异
template<typename T>
using json_converter = converter<T, TJSONProtocol>;

#endif // __THRIFT_HELPER_H__

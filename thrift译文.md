# Thrift：可扩展的跨语言服务实现（中文翻译)

## 摘要

Thrift 是一个由 Facebook 开发的软件库和代码生成工具集，旨在加快高效且可扩展的后端服务的开发与实现。它的主要目标是通过将每种编程语言中需要最多定制的部分抽象为一个在每种语言中实现的公共库，从而实现跨编程语言的高效可靠通信。具体而言，Thrift 允许开发人员在一个语言中立的文件中定义数据类型和服务接口，并生成构建 RPC 客户端和服务器所需的所有必要代码。

本文详细的说明了我们编写 Thrift 时的动机和设计选择，也包括一些更加有趣的实现细节。它不是一个研究论文，而是展示我们所作与所思。

## 1. 引言

随着 Facebook 的流量和网络结构的扩大，网站上许多操作（如搜索、广告选择和投放、事件记录）对资源的需求已经远远超出了 LAMP 框架的范围。在这些服务的实现中，我们选择了多种不同的编程语言，以便优化性能、简单快速的开发、复用现有的库。总体而言，Facebook 的工程文化倾向于选择最佳的工具和实现方式，而不是标准化于某种特定的编程语言，并阿Q般地接受这种语言带来的固有限制。

考虑到这样的设计选择，我们面临的挑战是设计透明、高性能的跨语言“桥梁”。我们发现大多数可用的方案要不是限制太多，就是没有提供足够自由的数据类型，或者性能不满足需要。

我们实施的解决方案结合了跨多种编程语言的语言中立软件栈和相关的代码生成引擎，该引擎将简单的接口和数据定义转换为客户端和服务器远程过程调用库。 选择静态代码生成而不是动态系统，使我们能够创建经过验证的代码，无需任何高级内省式运行时类型检查就可以运行。它还被设计得尽可能简单，开发人员通常可以在一个简短的文件中定义复杂服务所需的所有数据结构和接口。这种方式对开发人员来说非常便利。

考虑到这些相对常见的问题尚未存在一个稳定的开放解决方案，我们在初期就决定将 Thrift 开源。

为了评估在网络环境中的跨语言交互挑战，我们定义了一些关键的组件：

类型(*Types*)： 一个通用类型系统必须跨语言存在，不需要要求应用开发人员使用Thrift数据类型或者写专属的序列化代码。也就是说，一个 C++ 程序员应该能够透明地用一个强定义的 STL map 与一个 Python dictionary 进行数据交换。不需要强迫程序员为了使用这个系统而在应用层写任何其它代码。第2章详细描述了Thrift 的类型系统。

传输(*Transport*)： 每种语言必须有通用的接口来进行双向原始数据传输。服务开发人员不需要关心传输层如何实现，相同的应用层代码能够运行在 TCP 套接字、内存中的原始数据，或者磁盘文件。第3章详细描述了 Thrift 的传输层。

协议(*Protocol*)： 数据类型必须有一些方法来使用传输层进行编解码。同样，应用开发人员无需关心这一层。不管服务使用XML或者二进制协议，对于应用层代码来说都是无关紧要的。 关键是确保数据可以以一致且确定性的方式进行读写操作。第4章详细描述了 Thrift 的协议层。

版本管理(*Versioning*)： 对于健壮的服务，其中的数据类型必须提供一种机制来对其进行版本管理。尤其是它应该可以在不中断服务(或者，更坏的情况，出现段错误)的前提下，添加或删除一个对象中的字段，或者改变一个函数的参数列表。第5章详细描述了 Thrift 的版本管理系统。

处理器(*Processors*)： 最后，我们生成能够处理数据并完成远程过程调用的代码。第六章详细描述了生成的这些代码和处理器的示例。

第7章讨论了实现的细节，第8章进行总结。

## 2. 类型

Thrift 类型系统的目标是能够让程序员完全使用原生定义的类型，而不管他们使用何种编程语言。通过设计，Thrift 类型系统没有引入任何特殊的动态类型或者包装对象。它也不要求开发人员为对象序列化或传输写任何代码。Thrift IDL（Interface Definition Language，接口定义语言）文件在逻辑上是开发人员用最少的额外信息来注释其数据的一种方式，这些信息告诉代码生成器如何安全地在不同编程语言之间传输这些对象。

### 2.1 基本类型

类型系统依赖少量的基本类型。在考虑何种类型应该被支持时，我们的目标是简洁和简单而不是大而全，关注所有程序语言中都可用的关键类型，忽略只在特定语言可用的类型。

Thrift 支持的基本类型是：

- bool 表示一个布尔值，取 true 或 false
- byte 表示一个带符号字节
- i16 表示一个带符号16位整型
- i32 表示一个带符号32位整
- i64 表示一个带符号64位整
- double 表示一个带符号64位浮点数
- string 表示一个未知编码的文本或二进制串

值得注意的是没有无符号整型。由于这些类型在许多编程语言中没有直接的本地原始类型转换，因此它们所提供的优势会丧失。更进一步，没有办法阻止应用开发人员在诸如 Python 这样的语言中，把一个负值赋给一个整型变量 ，从而导致不可预测的情况。从设计的角度来看，我们观察到无符号整数很少用于算术目的，而更常用于作为键或标识符。在这种情况下，符号是无关紧要的。有符号整数可以起到同样的作用，并且在绝对必要时可以安全地转换为它们的无符号对应类型（这在C++中很常见）。

### 2.2 结构体

Thrift结构体定义了一个通用的对象，可以在不同的编程语言中使用。在面向对象语言中，一个结构本质上就是一个类。一个结构有一系列强定义字段，每个字段都有一个唯一的变量名（注：原文为Name，这里翻译成变量名比较恰当）。定义 Thrift 结构的基本语法看起来类似于C语言的结构定义。这些字段可以被一个整型字段标识注解（结构作用域所独有），也可选择使用默认值。如果忽略字段标识（Field Identifier），系统会自动分配，但是因为版本管理的原因，还是强烈推荐使用标识，这一点将会在后面讨论。

### 2.3 容器

Thrift容器是强类型容器，能够与常用语言中使用最通用的容器相对应。使用C++模板（或Java范型）的风格对其进行标注。在Thrift中，有三种可供使用的容器：

- list 一个有序元素列表。 直接翻译为一个 STL vector，Java ArrayList，或者脚本语言的原生数组。它可以包含重复元素。
- set 一个无序不重复元素集。翻译为 STL set，Java HashSet，Python set，或者 PHP/Ruby 中的原生 dictionary。
- map<type1,type2> 一个主键唯一的键值映射表。 翻译为 STL map，Java HashMap，PHP associative array，或者 Python/Ruby dictionary。

虽然提供默认容器，但是类型映射并不是明确固定的。为了在目标语言中替换自定义类型，已经添加了自定义代码生成器指令。(比如，hash map 或者谷歌的 sparse hash map 能够被用到 c++ 中)。唯一的要求是自定义类型支持所有必需的迭代原语。容器元素可以是任意合法的 Thrift 类型，甚至包括其它容器或者结构。

```c++
struct Example {
  1:i32 number=10,
  2:i64 bigNumber,
  3:double decimals,
  4:string name="thrifty"
}
```

在目标语言中，每个定义都会生成一个类型（类），其中包含两个方法：read 和 write。这些方法使用 Thrift TProtocol 对对象进行序列化和传输。

### 2.4 异常

异常在语法和功能上等价于结构体，唯一的不同是异常使用 exception 关键字而不是 struct 关键字声明。

生成的对象继承自各种目标编程语言中的一个恰当的异常基类（译者注：c++ std::exception），这样便可以无缝地与任意给定语言的原生异常整合。同样，设计的重点是生成对应用开发人员友好的代码。

### 2.5 服务

服务通过 Thrift 类型进行定义。一个服务的定义在语义上相当于面向对象编程中定义一个接口（或者一个纯虚抽象类）。Thrift 编译器生成完整的客户端代码和服务器端存根（server stub），用于实现接口。服务被定义如下：

```java
service <name> {
  <returntype> <name>(<arguments>)
  [throws (<exceptions>)]
 ...
}
```

（接下来看）一个例子：

```java
service StringCache {
  void set(1:i32 key, 2:string value),
  string get(1:i32 key) throws (1:KeyNotFound knf),
  void delete(1:i32 key)
}
```

注意 void 类型是除了所有已经被定义的 Thrift 类型外的一个合法的函数返回类型。如果 void 前面加上 async 关键字修饰，那么将产生不需要等待服务器响应的代码。注意一个纯 void 类型函数将给客户端返回一个响应，以此来保证服务器端的操作已经完成。通过使用 async 方法调用，客户端将只是保证请求被成功的放到了传输层。（在很多传输场景下，这种固有的不可靠性是由于拜占庭将军问题导致的。因此，应用开发人员应该只在如下情况下谨慎使用 async：可以接受调用丢失或已知传输是非常可靠的）。（译者注：async 关键字已经被 oneway 取代）

值得注意的是，函数的参数列表和异常列表都被实现为 Thrift 结构体，所以这三种构造在语法和行为上都是相同的。（译者注：这里描述的三种构造指的是函数、参数列表和异常列表。函数是一个操作或方法，参数列表是函数接受的参数的清单，异常列表是函数可能抛出的异常的清单。在Thrift中，它们都被表示为Thrift结构体，并且它们在语法和行为上完全一致。）

## 3. 传输

生成的代码使用传输层来促进（facilitate ：促进、使便利）传送数据。

### 3.1 接口

在 Thrift 的实现中，一个关键设计选择是将传输层与代码生成层解耦。尽管 Thrift 是典型的通过流套接字被使用在 TCP/IP 协议栈上，并以此作为基础通信层，但是没有强制性的原因要求把这样的限制附加于系统中。使用抽象 I/O 层所带来的性能损失（粗略的说就是对于每个操作需要一个虚方法查找/函数调用），与直接使用 I/O 操作的代价相比是无关紧要的（典型的就是系统调用）。

从根本上讲，生成的 Thrift 代码只需要知道如何读写数据，而与数据的源和目的是无关的；它可以是一个套接字，一段共享内存，或者是一个本地磁盘上的文件。Thrift 传输层接口支持以下方法：

- open 打开传输层
- close 关闭传输层
- isOpen 指示传输层是否已经打开
- read 从传输层中读取数据
- write 向传输层中写入数据
- flush 强制执行任何待处理的写操作（Forces any pending writes）

还有一些其他方法没有在这里列出，那些方法用来帮助进行批量读和选择性地从生成的代码中发送一个读（或写） 操作已经完成的的信号。

除了上面的 TTransport 接口之外，还有一个 TServerTransport 接口被用来接受或者创建基本的传输对象。接口如下：

- open 打开传输层
- listen 开始侦听客户端连接
- accept 返回一个新的客户端传输对象
- close 关闭传输层

### 3.2 实现

传输层接口的设计旨在在任何编程语言中简单实现。应用程序开发人员可以根据需要轻松定义新的传输机制。

#### 3.2.1 TSocket

TSocket 类在所有目标语言中都有实现，它为 TCP/IP 套接字提供了一个通用的、简洁的接口。

#### 3.2.2 TFileTransport

TFileTransport 是一个磁盘文件数据流的抽象。它用来将收到的一系列 Thrift 请求写到磁盘文件中。磁盘数据可以从日志中重现，可用作后处理（post-processing）或复制（ 模拟）过去的事件。

#### 3.2.3 工具程序

传输层接口支持使用面向对象技术（比如：对象的组合）进行简单扩展。一些简单的工具包括 TBufferedTransport，它在底层传输上对写入和读取进行缓冲，TFramedTransport，它通过帧大小头部传输数据以实现分块优化或非阻塞操作，以及 TMemoryBuffer，它允许直接从进程所拥有的堆或栈内存中进行读写操作。

## 4. 协议

Thrift 中第二重要抽象是将数据结构与传输层表示分离。Thrift 在传输数据时，强制使用某种确定的消息结构，但对使用的协议编码是中立的。也就是说，无论数据是以XML、可读的ASCII还是紧凑的二进制格式进行编码，只要它支持一组固定的操作，允许生成的代码对其进行确定性读取和写入，那么这并不重要。

### 4.1 接口

Thrift 协议接口是非常易懂的。它基本上支持两条原则：1）双向顺序消息，和2）基本类型、容器和结构的编码。

```c++
writeMessageBegin(name, type, seq)
writeMessageEnd()  
writeStructBegin(name)  
writeStructEnd()  
writeFieldBegin(name, type, id)  
writeFieldEnd()
writeFieldStop()  
writeMapBegin(ktype, vtype, size)  
writeMapEnd()  
writeListBegin(etype, size)  
writeListEnd()  
writeSetBegin(etype, size)  
writeSetEnd()
writeBool(bool)  
writeByte(byte)  
writeI16(i16)  
writeI32(i32)  
writeI64(i64)  
writeDouble(double)  
writeString(string)
```

```c++
name, type, seq = readMessageBegin()
                  readMessageEnd()

name =            readStructBegin() 
                  readStructEnd()
name, type, id =  readFieldBegin()
                  readFieldEnd()
k, v, size =      readMapBegin()
                  readMapEnd()
etype, size =     readListBegin()
                  readListEnd()
etype, size =     readSetBegin()
                  readSetEnd()
bool =            readBool()
byte =            readByte()
i16 =             readI16()
i32 =             readI32()
i64 =             readI64()
double =          readDouble()  
string =          readString()
```

注意除了 `writeFieldStop()` 外，每个 write 函数都有一个 read 函数与之相对应。`writeFieldStop()` 是一个特殊的方法，用来发送结构体结束信号。解析结构体的过程是从 `readFiledBegin()` 开始，直到遇到 stop 字段，然后调用 `readStructEnd()`。生成的代码依赖于这个调用序列，并以此确保所有内容已被协议编码器写入，这些被写入的内容可以通过相匹配的协议解码器读出。需要进一步说明的是这组函数设计得比实际需要的更健壮。 比如，`writeStructEnd()` 不是严格需要的， 因为一个结构的结尾可能会有一个隐含的停止字段。这个方法为冗长的协议提供了便利，在这些协议中，可以更加干净地分割这些调用（比如，在XML中的闭合标记< /struct>）。

### 4.2 结构

Thrift 结构被设计为支持编码到流式协议中。在对结构进行编码之前，不需要对其进行分帧或计算整个数据长度。在很多场景下，这对性能至关重要。考虑一个包含大量较大字符串的列表，如果协议接口要求读取或写入是一个原子操作，那么在编码任何数据之前，就需要对整个列表进行线性扫描。然而，如果列表可以在迭代时进行写入，相应的读取可以并行开始，从理论上提供了一个（kN - C）的端到端加速，其中N是列表的大小，k是序列化单个元素的成本因子，C是数据写入和可读取之间固定的延迟偏移量。

类似地，结构体不会事先编码其数据长度。相反，它们被编码为一系列字段，每个字段都有一个类型说明符和一个唯一的字段标识符。注意，包含类型说明符允许协议在没有任何生成的代码或访问原始IDL文件的情况下进行安全解析和解码。结构体以具有特殊 STOP 类型的字段头结束。由于所有基本类型都可以以确定性方式读取，所有结构体（即使包含其他结构体）也可以以确定性方式读取。Thrift 协议是自限定（self-delimiting）的，不需要任何分帧（framing），也不需要考虑编码的格式。

在那些不需要流或者需要分帧的场景， 使用 TFramedTransport 抽象是很容易把它添加到传输层的。

### 4.3 实现

Facebook已经实现和部署了一个有效利用空间的二进制协议，并应用在大多数后端服务中。本质上，它以普通二进制格式写所有数据。整型被转换成网络字节顺序，字符串则在前面添加字节长度，所有的消息和字段头都使用原始整数序列化构造进行写入。字段的字符串名被删除，当使用生成代码时，字段标识已经足够。

我们没有采用一些极端的存储优化方案（比如把小整型打包到ASCII码中，或者使用7位拓展格式）的原因是为了编码的简单和简洁。当我们需要进行这种优化时，这些改变也很容易实现。（注：TCompactProtocol）

## 5. 版本管理

在版本和数据定义发生变化的情况下，Thrift 具有很强的鲁棒性。这对于实现部署服务的分阶段更新（滚动升级）是至关重要的。系统必须能够支持从日志文件中读取旧数据，以及处理来自过期客户端的请求，反之亦然。

### 5.1 字段标识符

Thrift 通过字段标识符来实现版本管理。对于每个被 Thrift 编码的结构的域头，都有一个唯一的字段标识符。这个字段标识符和它的类型说明符构成了对这个字段的唯一性标识。Thrift 定义支持字段标识符自动分配，但良好的程序实践告诉我们应该明确的指出字段标识符。字段标识符使用如下方法指定：

```java
struct Example {  
  1:i32 number=10,  
  2:i64 bigNumber,  
  3:double decimals,
  4:string name="thrifty"
}
```

为了避免手动分配和自动分配标识符之间的冲突，省略标识符的字段被分配为从-1开始递减，并且手动分配的标识符只支持正数（注：系统自动分配的使用负数）。

当数据进行反序列化时，生成的代码可以使用这些标识符来正确识别字段，并确定它是否与其定义文件中的字段一致。如果无法识别字段标识符，生成的代码可以使用类型说明符跳过未知字段而不会引发任何错误。再次强调，这是由于所有数据类型都是自限定的特性所实现的。

字段标识符可以（也应该）在函数参数列表中指定。实际上，参数列表不仅在后端表示为结构体，而且在编译器前端实际上也共享这些相同的代码。这是为了允许安全地修改方法的参数。

```c++
service StringCache {
  void set(1:i32 key, 2:string value),
  string get(1:i32 key) throws (1:KeyNotFound knf),
  void delete(1:i32 key)
}
```

选择字段标识符的语法是为了反映它们的结构。可以将结构体视为一个字典，其中标识符是键，值是具有强类型命名的字段。

字段标识符内部使用 i16 数据类型表示，然而需要注意的是，TProtocol 抽象可能会以任何格式对标识符进行编码。

### 5.2 Isset

当遇到一个未期望的字段，能够被安全的忽略和丢弃。 当一个预期的字段未被找到，必须有一些方法来向告知开发者该字段未出现。这是通过内部结构 isset 来实现的，这个结构位于已定义对象内部（注：新的 Thrift 版本 isset 定义在结构体外部）。（Isset 的函数特性通过一个隐含空值，如 PHP 中的 null，Python 中的 none，Ruby 中的 nil）本质上说，每个 Thrift 结构内部的 isset 对象为每个字段包含了一个布尔值，以此指示这个字段是否出现在结构中。当读取器接收到一个结构体时，在直接操作结构体之前，应该首先检查字段是否已经设置。

```c++
class Example {  
  public:  
    Example() :
      number(10),  
      bigNumber(0),  
      decimals(0),  
      name("thrifty") {}

    int32_t number;  
    int64_t bigNumber;  
    double decimals;  
    std::string name;

    struct __isset {
      __isset():  
        number(false),  
        bigNumber(false),  
        decimals(false),  
        name(false) {}
        bool number;
        bool bigNumber;
        bool decimals;  
        bool name;
    } __isset;
...
}
```

### 5.3 案例分析

版本不匹配可能发生在如下四种情况中：

1. 添加字段，旧客户端，新服务器。在这种情况下，旧客户端没有发送新字段。新服务器识别到那个新字段未设置，执行对于旧数据请求的默认操作。
2. 删除字段，旧客户端，新服务器。在这种情况下，旧客户端发送已被删除的字段。新服务器简单地忽略这个字段。
3. 添加字段，新客户端，旧服务器。新客户端发送了旧服务器不能识别的字段，旧服务器简单的忽略，并按正常请求处理。
4. 删除字段，新客户端，旧服务器。这是最危险的情况，因为旧服务器很可能没有为缺失字段实现适当的默认行为。建议在这种情况下先部署新服务器，然后再部署新客户端。

### 5.4 协议/传输版本管理

TProtocol 抽象还旨在赋予协议实现自由选择以任何方式对其进行版本控制。具体而言，在`writeMessageBegin()`调用中，任何协议实现都可以自由发送其选择的内容。如何处理协议级别的版本控制完全取决于实现者。关键点在于协议编码变化与接口定义版本变化是安全隔离的。

需要注意的是，TTransport 接口也具有完全相同的特性。例如，如果我们希望向 TFileTransport 添加一些新的校验和错误检测功能，我们可以简单地在写入文件的数据中添加一个版本头，以便它仍然可以接受旧的不带有给定版本头的日志文件。

## 6. RPC实现

### 6.1 TProcessor

在 Thrift 设计中，最后一个核心接口是 `TProcessor`，可能是最简单的接口之一。接口如下：

```c++
interface TProcessor {
  bool process(TProtocol in, TProtocol out)
    throws TException
}
```

这里的关键设计理念是，我们构建的复杂系统基本上可以被分解为在输入和输出上操作的代理或服务。在大多数情况下，实际上只有一个输入和输出（一个RPC客户端）需要处理。

### 6.2 生成代码

当定义一个服务时，我们使用一些辅助工具（通常是指实现接口服务的那些Helper类，译者注）生成一个能够处理该服务的RPC请求的 `TProcessor` 实例。基本结构（以C++伪代码为例）如下：

```c++
Service.thrift
  => Service.cpp  
    interface ServiceIf
    class ServiceClient : virtual ServiceIf  
      TProtocol in
      TProtocol out
    class ServiceProcessor : TProcessor  
      ServiceIf handler
```

ServiceHandler.cpp

```java
class ServiceHandler : virtual ServiceIf
```

TServer.cpp

```c++
TServer(TProcessor processor,  
  TServerTransport transport,  
  TTransportFactory tfactory,  
  TProtocolFactory pfactory)
serve()
```

从Thrift定义文件中，我们生成虚拟服务接口。生成一个客户端类，该类实现了接口并使用两个 `TProtocol` 实例执行I/O操作。生成的处理器实现了 `TProcessor` 接口。生成的代码具备处理RPC调用的逻辑，通过 `process()` 方法进行调用，并以应用程序开发者实现的服务接口实例作为参数。

用户在独立的非生成源代码中提供应用程序接口的实现（注：在独立的文件中继承接口并实现方法，如上面示例的ServiceHandler.cpp文件）。

### 6.3 TServer

最终，Thrift 核心库提供一个 `TServer` 抽象。这个 `TServer` 对象通常以如下方式运行：

- 使用 `TServerTransport` 获取一个 `TTransport`
- 使用 `TTransportFactory` 可以选择将原始传输转换为适合的应用程序传输方式（通常在这里使用 `TBufferedTransportFactory`）
- 使用 `TProtocolFactory` 为 `TTransport` 创建输入和输出协议
- 调用 `TProcessor` 对象的 `process()` 方法

这些层级被适当地分离，以便服务代码无需了解任何正在使用的传输方式、编码方式或应用程序。服务器封装了与连接处理、线程等相关的逻辑，而处理器负责处理RPC。Thrift 定义文件和接口实现是应用开发人员唯一需要写代码的地方。

Facebook 已经部署了多种 `TServer` 实现， 包括单线程的 `TSimpleServer`，每个连接一个线程的 `TThreadedServer`，和线程池 `TThreadPoolServer` 模型。

`TProcessor` 接口在设计上非常通用。没有要求 `TServer` 接受生成的 `TProcessor` 对象。Thrift 允许应用程序开发者轻松编写任何基于 `TProtocol` 对象操作的服务器（例如，服务器可以仅简单地流式传输某种类型的对象而无需进行实际的RPC方法调用）。

## 7. 实现细节

### 7.1 目标语言

Thrift目前支 5种目标语言：C++，Java，Python，Ruby，和PHP（注：截止0.16.0版本，已经实现了28种语言）。在Facebook，我们已经部署的服务器主要使用C++，  Java和Python。在PHP中实现的Thrift服务也已经嵌入到了Apache web服务器中，用来让后端通过THttpClient透明地访问我们前端结构, THttpClient实现了TTransport的接口。

尽管Thrift被明确地设计得比典型的web技术更高效和健壮，但是随着我们设计一个基于XML的REST web服务API， 我们注意到Thrift能够容易地用来定义我们的服务接口。尽管我们目前没有使用SOAP封套元素（在作者们的眼中，已经有过多重复的Java软件做这一堆事情），但是我们能够快地为我们的服务扩展Thrift，使其产生XML概要定义文件， Thrift 是一个为我们web服务的不同实现进行版本管理的框架。尽管公共web服务诚然于Thrift的核心用例和设计无关， 但是Thrift能够便利地快速迭代，并使我们有能力快速地将整个基于XML的web服务迁往更高性能的系统。

### 7.2 生成的结构

我们自觉的将产生的结构尽可能的透明。所以的字段都是公共可访问的；没有set()和get()方 。类似，isset对象的使用不是强制性的。我们没有包含任何FieldNotSetException这样的结构。开发人员可以选择这些字段写出更健壮的程序，  但是系统对于开发人员完全忽略isset结构也是健壮的，并且将对所有情况提供一个恰当的默认行为。

这种选择来源于对简便应用开发的渴望。我们陈述设计目标不是为了让开发人员在他们选择的语言中学习一种新的丰富的库，而是产生代码允许他们在每种语言中使用他们最熟悉的结构。

我们也把产生的对象的read()和write()方法放置在公共作用域中，以便这个对象能够被RPC客户端和服务器端的外部上下文使用。 能够产生跨语言容易序列化的对象这一点来说，Thrift 是一个有用的工具。

### 7.3 RPC方法识别

在RPC中，方法用是通过发送该方法名的字符串来实现的。一个关于这种方法的议题是：更长的方法名意味需要更大的带宽。我们实验过使用固定长度的hash值去识别方法名，但是最后发现节省的带宽与引起的问题相比是不划算的。如果不使用元数据存储系统，要可靠地处理接口定义文件的跨版本冲突是不可能的。（比如，为了给一个文件的当前版本产生无冲突的hash值，我们可能必须知道这个文件先前已经存在的所有版本的冲突）。

我们想避免在方法调用过程中过多没有必要的字符串比较。为了解决这个问题，我们产生了从字符串到函数指针的映射，以便在通常情况下，调用可以通过固定时间的hash查找高效地完成。这就要求使用一个二元代码结构。因为Java没有函数指针，处理函数在实现了一个通用接口的所有私有成员类中。

```java
private class ping implements ProcessFunction {  
  public void process(int seqid,
                      TProtocol iprot,  
                      TProtocol oprot)
    throws TException
  { ...}
}

HashMap<String,ProcessFunction> processMap_ =
  new HashMap<String,ProcessFunction>();
```

在C++中，我们使用了一个相对孤僻的语言结构：成员函数指针。

```c++
std::map<std::string,
  void (ExampleServiceProcessor::*)(int32_t,  
  facebook::thrift::protocol::TProtocol*,  
  facebook::thrift::protocol::TProtocol*)>  
processMap_;
```

通过这些技术，处理字符串的代价被最小化了，因为知道了字符串方法名，所以我们还得到了这样的好处：可以容易地通过检查方法名， 调试崩溃或错误的数据。

### 7.4 服务端和多线程

Thrift服务要求基本的多线程来处理来多个客户端的同时请求。 对于实现Thrift服务器逻辑的Python和Java代码，对应语言的标准线程库就已经提供了足够的支持。对 于C++的实现，没有标准的多线程运行时库存在。特别是没有健壮的、轻量级的和易用的线程与定时类实现。我们考察了存在的一些实现， 即：boost::thread， boost::threadpool， ACE Thread Manager和ACE Timer。

虽然boost::threads提供了简洁、轻量级和健壮的多线程原语实现(互斥、条件变 、线程)，但是它没有提供线程管理或定时器实现。

boost::threadpool也是看起来很有希望，但是离我们想要的还差很远。我们想尽可能的减少对第三方库的依赖，因为boost::threadpool不是一个标准的模板库，它要求运行时库并且它还不是Boost官方发行版的一部分，所以我们感觉它还不能用到Thrift中。 boost::threadpool的发展，尤其是如果它被添加到了Boost的发行版，我们也许会重新考虑是不是要用它。

ACE除了提供多线程原语外，还有有一个线程管理类，也有定时器类。但是ACE最大的问题是它本身。和Boost不同，  ACE API的质量是不够的。ACE中的任何东西都对ACE中的其它任何东西有大量的依赖，因此强迫开发人员扔掉标准类，比如STL容器，而选择ACE的专属实现。另外，不同于Boost，  ACE的实现显示了较弱的可理解性和C++编程的一些缺陷，没有利用现代模板技术来确保编译时安全并把一些编译错误信息合理化。因为这些原因，ACE不被选择。作为替代，我们选择实现我们自己的库，并在接下来的几节中描述。

### 7.5 线程原语

Thrift线程库在facebook::thrift::concurrency名字空间下实现，并由三部分构成：

- 原语
- 线程池管理器
- 定时器管理器

如上所述，我们对于引入任何外部依赖到Thrift是犹豫不决的。尽管如此，我们还是决定使用boost::shared ptr，因为它对于多线程应用是如此的有用，它不要求链接时或运行时库（即，他是一个纯 库）并且它将成为C++0x标准的一部分。

我们实现了标准的互斥和条件类， 还有一个监控类。 后者是对一个互斥和条件变量的简单组合， 类似于对Java  Object类提供的Monitor的实现。这有时也被看成是一个障碍。 我们提供了一个同步守护类，以便允许类 Java一样的同步块。这只是语法上的一点好处，但是， 它的Java对应部分一样，清楚地界定代码的临界区。和它的Java对应部分不一样的是，我们仍然可以在编程中使用lock，unlock，block和signal  monitors。

```java
void run() {
  {Synchronized s(manager->monitor);
    if (manager->state == TimerManager::STARTING) {  
      manager->state = TimerManager::STARTED;  
      manager->monitor.notifyAll();
    }
  }
}
```

我们再次借鉴了Java中线程和一个runnable类的区别。一 个线程实际是一个可调度对象。Runnalbe是线程运行中的逻辑。线程实现处理的是所有平台下的线程创建和销毁问题，而Runnalbe实现处理的是每个线程逻辑下的特定应用。这种方法的好处是，开发人员可以容易地编写Runnable类的子类，而不需要纠缠于特定平台的超类中。

### 7.6 线程、Runnable和shared ptr

我们在ThreadManager和TimerManager实现的整改过程都使用了boost::shared ptr，以此来保证被多线程访问的死对象已经被清理干净。对于Thread类的实现，boost::shared ptr的用法要求特别注意确保Thread对象们在创建或关闭线程时没有泄漏也没有被过早引用。

线程创建要求在C库中调用（在我们的例子中是POSIX线程库、libpthread，但是对于WIN32线程也可能是一样的）。典型地， 操作系统在调用ThreadMain（C语言线程入口函数）时，几乎无法做到（时间上的）保证。因此，我们在线程创建时调用ThreadFactory::newThread()，可能在系统调用之前就给调用者返回了正常。如果调用者在ThreadMain 用之前放弃了引用，为了确保返回的Thread对象不会提前被过早清除， Thread对象在它的start方法中，对它自己做了一个弱引用。

如果持有弱引用， 那么ThreadMain函数可以在进入绑定到Thread的Runnalbe对象的Runnalbe::run方法前， 尝试获取一个强引用。 如果对于这个线程没有强引用能够在退出Thread::start与进入ThreadMain之间被获得，那么弱引用返回null，函数立即退出。

Thread对自身建立一个弱引用的需求在API层面影响深远。因为引用是通过boost::shared ptr 被管理， Thread对象必须有一个关于它自己的引用， 这个引用同样被一 个boost::shared ptr封装，并返回给调用者。这就促使了对工厂模式的使用。ThreadFactory创建了这个原始Thread对象和 一个boost::shared ptr包装器，并通过调用一个私有helper方法允许它通过boost::shared ptr封装建立一个到它自身的弱引用， 这个类实现了Thread接口(在本例中是PosixThread::weakRef)。

Thread和Runnalbe对象相互引用。一个Runnable对象可能需要知道那个它在其中执行的线程的信息。对于一个线程，显然地需要知道那个使它持有的Runnable对象。这种内部依赖更加复杂，因为每个对象的生命周期是独立于其它对象的。 一个应用有可能创建了一个Runnable对象集，并将其重用到不同的线程中，或者，一旦一个线程已经为一个Runnalbe对 象被创建并开始运行，但有可能再去创建，或者忘记创建那个已经被创建的Runnalbe对象。

当Runnalbe类有一个明 的线程方 允许对明确的有线程绑定时， Thread类在它的构造函数中带了一 个boost::shared ptr引用到它 有的Runnable对象中。 ThreadFactory::newThread负责了Thread对象到每个Runnalbe对象的绑定工作。

### 7.7 线程管理器

线程管理器创建了一个生产者线程池并且允许应用程序 在空闲生产者线程可用时 、执行任务。线程管理器没有实现动态线程池重新 节大小，但是提供了一些原语以便应用能够根据负载添加和删除线程。这样做是因为实现负载和线程池调整是应用相关的。比如，有些应用可能想根据通常运行情况下的任务到达率调整线程池大小，任务到达率通过轮询采样得出。 一些应用可能望简单地通过工作堆列的深度和高低水位立即作出反应。与其尽量创建足够处理这些不同方的一个复杂的API抽象，不如我们简单地让特定应用决定如何 ，并提供特定的原语去实现想要的策略并抽样当前的状态。

### 7.8 定时器管理器

定时器管理器允许应用程序 Runnable对象在未来的某个时刻执行。它的具体任务是允许应用程序以固定 期抽样ThreadManager的负载，并根据应用策略调整线程池的大小。当然它可以用来产生任意数量的定时器和事件告警。

定时器管理器的默认实现是使用一个单线程去执行到期的Runnalbe对象。因此，如果一个定期器操作需要做大量的工作，尤其是做阻塞性I/O操作的话，那就应该在另外的线程中来做。

### 7.9 非阻塞操作

尽管Thrift传输接口更多的是直接映射到阻塞I/O 型，但是我们在C++实现了一个基于libevent和TFramedTransport的高性能TNonBlockingServer。我们通过把I/O操作移动到一个紧凑的事件循环中实现了它，事件循环使用状态机表示。本质上，事件循环读TMemoryBuffer对象中编好帧的请求，一旦整 个请求准备好他们就被分发给TProcessor对象，TProcessor对象可以直接从内存中读取数据。

### 7.10 编译器

Thrift编译器是通过使用lex/yacc进行词法分析和解析，并由C++实现的。尽管可以使用代码行数更少的其他语言实现（也 是Python Lex-Yacc（PLY）或者ocalmlyacc），但是我们使用C++强制对语言构造函数的 示定义。强定义的解析树元素（有争议地）使代码对于新开发者更友好。

代码产生分为两个过程，第一个过程只为头文件和类型定义的目的扫描。类型定义在这个阶段不会被检查，因为它们可能依赖头文件。在第一个过程，所有头文件都是被顺序地扫描。一旦头文件树已经被决定，那么对于所有文件的第二个过程是将类型定义插入到解析树中，并且对任何未定义的类型报错。然后程序对照解析树被产生。

由于固有的复杂性和存在循环依赖的潜在危险，我们明确地禁止前向声明。两个Thrift结构不能相互包含彼此的实例。（因为我们在产生的C++代码中，不允许空结构实例，这实际是不可能的）

### 7.11 TFileTransport

TFileTransport根据输入数据的长度将其编帧并写到磁盘上，以此记录Thrift的请求/结构。通过一种磁盘上编帧的格式允许更好的错误检查，并帮助处理有限数量的离线事件。

当记录大量数据的时候，TFileTransport使用系统内存交换缓冲区确保良好的性能。一个Thrift日志文件被分割成若干特定大小的块；记录信息不允许跨越块的边界。一个可能引起跨越的消息将触发对该文件最后部分的填充，消息的第一个字节与下一个分块的开始对齐。对文件分块使 一个文件特定点开始读和解释数据成为可能。

## 8. Facebook Thrift服务

在Facebook,Thrifty已经被部 到大量应用中，包括搜索、 日志、移动，广告和开发者平台。下面将讨论两个特定应用。

### 8.1 搜索

Thrift被作为Facebook搜索服务的潜在协议和传输层。 对于搜索来说， 语言的产生非常适合， 因为他允许为应用开发使用一种高效的服务器端语言（C++） 并且允许Facebook基于PHP的web应用通过使用了Thrift PHP库调用搜索服务。也有有大量搜索状态、部署和调试性功能建立在产生的Python代码上。除此之外，Thrift日志文件格式被用作“重做日志”(redo log)来提供实时搜索索引的更新。Thrift已经允许搜索团队根据每种语言的特点来选择开发语言，并以此加快开发进 。

### 8.2 日志

Thrift TFileTransport的函数性是用来结构化日志的。每个服务函数与其参数的定义都能够被看作一个被函数名标识的结构化日志入口。这个日志能够在其后被广泛使用，包括在线和离线处理，状态聚集和作为“重做日志”(redo log)。

## 9. 结论

Thrift通过使工程师能够高效分治处理，而在Facebook建立起了可扩展的后端服务。应用开发人员可以关注于应用代码而不必担心套接字层。通过在同一个地方写缓存和I/O逻辑，我们避免了重复工作，这样比在应用程序中分散处理好。

Thrift已经在Facebook中的大量不同应用中部署，包括搜索、日志、移动广告和开发者平台。我们发现使用一个额外的软件抽象层所带来的微小花费与在开发效率和系统稳定性上的收获相比，微不足道。

## A. 类似的系统

下面是一些与Thrift类 的软件系统。每个都非常简略的 描述：

- SOAP. 基于XML，通过HTTP进行web服务，过 的XML解析消耗。
- CORBA. 使用相对广泛，由于过 设计和重 级受到争议。同样笨重的软件安装。
- COM. 主要在Windows客户端软件被选择，不是一个开放的解决方案。
- Pillar. 轻 级和高性能，但是没有版本管理和抽象。
- Protocol Buffers. Google拥有的闭源产品，在Swazall论文中被描述。

## 致谢

非常感谢Martin Smith, Karl Voskuil 和Yishan Wong对于Thrift的反馈。

Thrift是Pillar的继承者，这是个AdamD’Angelo开发的类系统，开始在Caltech公司后来在Facebook公司继续。如果没有Adam的洞察力，也 绝不会有Thrift了。

## References

1 Kempf, William, “Boost.Threads”, http://www.boost.org/doc/html/threads.html

2 Henkel, Philipp, “threadpool”, [http://threadpool.](http://threadpool/) [sourceforge.net](http://sourceforge.net/)
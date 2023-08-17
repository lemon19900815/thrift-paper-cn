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

Thrift目前支持5种目标语言：C++，Java，Python，Ruby，和PHP（注：截止0.16.0版本，已经实现了28种语言）。在Facebook，我们主要在C++、Java和Python中部署服务器。以PHP实现的Thrift服务也被嵌入到Apache Web服务器中，通过 `TTransport` 接口的 `THttpClient` 实现，为我们的前端构建提供了透明的后端访问。

尽管Thrift明确设计为比传统的Web技术更高效和稳健，但在设计基于XML的REST Web服务API时，我们注意到可以很容易地使用Thrift来定义我们的服务接口。虽然我们目前没有使用SOAP封装（在作者的观点中，已经有太多重复的企业级Java软件来完成这种任务），但我们能够快速扩展Thrift来生成我们服务的XML Schema Definition文件，并为不同实现版本的Web服务提供框架支持。尽管公共Web服务与Thrift的核心用例和设计明显不相关，但Thrift能够便利地快速迭代，并使我们能够在需要时将整个基于XML的Web服务迁移到性能更高的系统上。

### 7.2 生成的结构体

我们有意决定使生成的结构体尽可能透明。所有字段都是公开可访问的，没有 `set()` 和 `get()` 方法。同样，并不强制使用 isset 对象。我们没有任何`FieldNotSetException` 这样的构造。开发人员可以选择使用这些字段来编写更健壮的代码，但系统对于开发人员完全忽略 isset 构造也是很健壮的，并且在所有情况下都会提供适当的默认行为。

这一选择的动机是希望简化应用程序开发。我们的目标不是让开发人员用他们选择的语言学习一种全新的丰富库，而是生成的代码允许他们在每种语言中使用他们最熟悉的结构。

我们还将生成对象的 `read()` 和 `write()` 方法设置为 public，以便这些对象可以在RPC客户端和服务器的上下文之外使用。Thrift可以作为跨语言之间易于序列化对象的一个有用工具。

### 7.3 RPC方法定义

在RPC中，方法调用是通过将方法名作为字符串发送来实现的。采用这种方法的一个问题是，较长的方法名需要更多的带宽。我们尝试过使用固定大小的哈希来标识方法，但最终得出的结论是所节省的开销不足以抵消带来的麻烦。要可靠地处理接口定义文件的不同版本之间的冲突是不可能的，除非有一个元存储系统（即为了为当前版本的文件生成不冲突的哈希，我们还需要了解先前任何版本的文件中存在的所有冲突）。

我们希望避免在方法调用时进行过多没有必要字符串比较。为了解决这个问题，我们生成了从字符串到函数指针的映射，这样在通常情况下，调用可以通过固定时间的hash查找高效地完成。这就需要使用一些有趣的代码构造，因为Java没有函数指针，所以处理函数都是实现了一个公共接口的私有成员类。

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

利用这些技术，字符串处理的成本被最小化，我们能够通过检查已知的字符串方法名来方便地调试程序中断或错误的数据，并从中受益。

### 7.4 服务端和多线程

Thrift服务需要基本的多线程来处理多个客户端的并发请求。对于 Python 和 Java 实现的Thrift服务器逻辑，语言自带的标准线程库提供了足够的支持。但对于C++，没有标准的多线程运行时库。具体而言，健壮、轻量级和可移植的线程管理器和定时器类实现并不存在。我们调查了现有的实现，包括 `boost::thread`、`boost::threadpool`、`ACE Thread Manager` 和 `ACE Timer`。

虽然 `boost::threads` 提供了简洁、轻量级和健壮的多线程原语实现（`mutex, condition, thread`），但是它没有提供线程管理或定时器实现。

`boost::threadpool`[2]看起来很有希望，但对于我们来说还不够成熟。我们希望尽量减少对第三方库的依赖。由于 `boost::threadpool` 不是一个纯模板库，需要运行时库，并且它还没有成为官方Boost发行版的一部分，我们认为它还不适合在Thrift中使用。随着 `boost::threadpool` 的发展，特别是如果它被添加到Boost发行版中，我们可能会重新考虑不使用它的决定。

ACE除了提供多线程原语外，还具有线程管理器和定时器类。ACE的最大问题是它本身，与Boost不同，ACE的API质量很差。ACE中的所有内容都与ACE中的其他内容有大量的依赖关系，从而迫使开发人员放弃标准库（比如STL容器），而选择ACE的专属实现。此外，与Boost不同，ACE的实现对C++编程的威力和陷阱了解甚少，并且没有充分利用现代模板技术来确保编译时安全性和合理的编译器错误信息。基于这些原因，我们没有选择ACE。相反，我们选择实现我们自己的库，并在接下来的几节中描述。

### 7.5 线程原语

Thrift线程库在 `facebook::thrift::concurrency` 名字空间下实现，并由三部分构成：

- 原语（`mutex, condition, thread`）
- 线程池管理器
- 定时器管理器

如上所述，我们对于引入任何外部依赖到Thrift是犹豫不决的。尽管如此，我们还是决定使用 `boost::shared_ptr`，因为它对于多线程应用是如此的有用，它不要求链接或运行时库（即，它是一个纯模板库）并且它将成为C++0x标准的一部分。

我们实现了标准的 `mutex` 和 `condition`， 还有一个`monitor`监控类。 后者是对一个互斥锁和条件变量的简单组合， 类似于对Java Object类提供的 Monitor 的实现。这有时也被看成是一个屏障（`barrier`，译者注：请参考内存屏障相关概念）。我们提供了一个Synchronized保护类，以实现类似 Java 中的同步块功能。这只是一点语法糖，像Java等效功能或特性（Java counterpart）一样，清晰地定义了代码的关键部分。与其Java对应物不同的是，我们仍然具有以编程方式锁定、解锁、阻塞和发送监视器的能力。

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

我们再次借鉴了Java中线程（Thread）和可运行类（Runnable Class）之间的区别。线程（Thread）是实际可调度的对象，而可运行类（Runnable）是在线程中执行的逻辑。线程（Thread）的实现处理了与特定平台相关的线程创建和销毁问题，而可运行类（Runnable）的实现处理了针对每个线程的应用程序特定逻辑。这种方法的好处是，开发人员可以轻松地编写Runnable类的子类，而无需引入特定平台的超类。

### 7.6 Thread、Runnable、and shared_ptr

在 `ThreadManager` 和 `TimerManager` 的实现中，我们使用 `boost::shared_ptr` 来确保对可以被多个线程访问的已失效对象进行清理。对于Thread类的实现，使用 `boost::shared_ptr` 需要特别注意，在创建和关闭线程时要确保Thread对象既不会泄漏也不会过早地被解引用。

Thread的创建需要调用C库（在我们的例子中是POSIX线程库 `libpthread`，但对于WIN32线程也是如此）。通常，操作系统对于何时调用C线程的入口函数`ThreadMain` 没有提供任何保证。因此，我们在线程创建时调用 `ThreadFactory::newThread()` ，可能会在系统调用之前返还回到调用者。如果调用者在`ThreadMain` 调用之前放弃了引用，为了确保返回的Thread对象不会提前被清理，Thread对象在start方法中对自己进行了弱引用（`weak_ptr`）。

有了弱引用，`ThreadMain` 函数可以在进入绑定到线程的Runnable对象的Runnable::run方法之前，尝试获取一个强引用（`shared_ptr = weak_ptr.lock`）。如果在退出 `Thread::start` 和进入 `ThreadMain` 之间没有获取到对该线程的强引用（`shared_ptr`指针为空），弱引用将返回null，并且函数立即退出。

Thread需要对自身进行弱引用对API有着重要的影响。由于引用是通过 `boost::shared_ptr` 模板进行管理，Thread对象必须通过相同的 `boost::shared_ptr`包装器拥有对自身的引用，并将其返回给调用者。这就促使了对工厂模式的使用。`ThreadFactory` 创建原始的Thread对象和 `boost::shared_ptr` 包装器，并通过调用一个私有helper方法允许它通过 `boost::shared_ptr` 封装建立一个到它自身的弱引用， 这个类实现了Thread接口(在本例中是 PosixThread::weakRef)。

Thread和Runnable对象相互引用。一个Runnable对象可能需要知道那个它在哪个线程执行。对于一个线程，显然也需要知道它持有的Runnable对象。这种内部依赖更加复杂，因为每个对象的生命周期是独立于其它对象的。 一个应用程序可以创建一组可在不同线程中重复使用的Runnable对象，或者在创建和启动线程后就创建并遗忘了一个Runnable对象。

Thread类在其构造函数中接受对托管的Runnable对象的 `boost::shared_ptr` 引用，而Runnable类有一个显式的thread方法，允许显式绑定托管的线程。`ThreadFactory::newThread` 则将这些对象绑定在一起。

### 7.7 ThreadManager

ThreadManager创建了一个工作线程池，并允许应用程序在可用的空闲工作线程中安排任务执行。ThreadManager不实现动态线程池调整，但提供了基本操作，以便应用程序可以根据负载情况添加和删除线程。选择这种方法是因为实现负载度量和线程池大小非常依赖于具体的应用程序。例如，某些应用程序可能希望基于通过轮询样本测量到的工作到达率的滚动平均值来调整线程池大小。其他应用程序可能只希望立即对工作队列深度的高低水位进行反应。与其试图创建一个足够抽象以涵盖这些不同方法的复杂API，不如让特定的应用程序决定如何处理，而我们只提供所需策略的基本操作和对当前状态的采样。

### 7.8 TimerManager

TimerManager允许应用程序安排Runnable对象在将来的某个时间点执行。它的具体任务是允许应用程序定期采样ThreadManager的负载，并根据应用程序策略对线程池大小进行更改。当然，它也可以用于生成任意数量的定时器或警报事件。

TimerManager的默认实现是使用一个单线程去执行到期的Runnable对象。因此，如果一个定期器操作需要做大量的工作，尤其是做阻塞性I/O操作的话，那就应该在另外的线程中来做。

### 7.9 非阻塞操作

尽管Thrift传输接口更直接地映射到阻塞式I/O模型，但我们基于 `libevent` 和 `TFramedTransport` 在C++中实现了高性能的 `TNonBlockingServer`。我们通过将所有I/O移动到一个紧凑的事件循环中，使用状态机来实现这一点。基本上，事件循环将帧请求读入 `TMemoryBuffer` 对象中，一旦整个请求就绪，它们将被分发到 `TProcessor` 对象，该对象可以直接从内存中读取数据。

### 7.10 编译器

Thrift编译器是使用标准的lex/yacc词法分析和语法分析在C++中实现的。尽管在其他语言（如Python Lex-Yacc（PLY）或ocamlyacc）中可以用更少的代码实现，但使用C++可以明确定义语言结构。强类型定义对解析树元素（有争议地）使得代码更易于理解，对新开发人员更友好。

代码生成分为两个阶段。第一阶段仅查找包含文件和类型定义。在此阶段不检查类型定义，因为它们可能依赖于包含的文件。在第一个过程，所有头文件都是被顺序地扫描。一旦解析了包含树，就会对所有文件进行第二遍扫描，将类型定义插入解析树，并且对任何未定义的类型报错。然后根据解析树生成代码。

由于固有的复杂性和潜在的循环依赖性，我们明确禁止前向声明。两个Thrift结构体不能同时包含彼此的实例。（由于我们不允许在生成的C++代码中使用空的结构体实例，这实际上是不可能的。）

### 7.11 TFileTransport

`TFileTransport` 根据输入数据的长度将其编帧并写到磁盘上，以此记录Thrift的请求/结构。通过一种磁盘上编帧的格式允许更好的错误检查，并帮助处理有限数量的离线事件。`TFileTransport` 根据输入数据的长度将其编帧并写到磁盘上，以此记录Thrift请求/结构。使用编帧的磁盘格式可以更好地进行错误检查，并有助于处理有限数量的离散事件。`TFileWriterTransport` 使用内存缓冲区的交换系统，以确保在记录大量数据时获得良好的性能。一个Thrift日志文件被分成指定大小的块；日志消息不允许跨越块边界。如果一个消息将要跨越块的边界，将在消息的末尾添加填充，直到块的末尾和消息的第一个字节与下一个块的开始对齐。将文件分成块使得可以从文件的特定位置读取和解释数据。

## 8. Facebook Thrift Service

在Facebook，Thrift已经被部署到大量应用中，包括搜索、日志、移动应用，广告和开发者平台。下面将讨论两个特定应用。

### 8.1 搜索

Thrift被用作Facebook搜索服务的底层协议和传输层。多语言代码生成非常适合搜索，因为它允许在高效的服务器端语言（如C++）中开发应用程序，并允许基于PHP的Facebook Web应用程序通过使用Thrift PHP库调用搜索服务。此外，还有大量的搜索统计、部署和测试功能是构建在生成的Python代码之上的。此外，Thrift日志文件格式还用作提供实时搜索索引更新的重做日志。Thrift使搜索团队能够充分利用各种语言的优势，并以快速的速度开发代码。

### 8.2 日志

Thrift的 `TFileTransport` 功能用于结构化日志记录。每个服务函数定义以及其参数可以被视为由函数名称标识的结构化日志入口。这个日志可以用于各种目的，包括在线和离线处理、统计信息聚合以及作为重做日志（redo log）。

## 9. 结论

Thrift使Facebook能够使用分而治之策略来高效地构建可扩展的后端服务。应用程序开发人员可以专注于应用程序代码，而不必担心底层的套接字层。通过在同一个地方写缓存和I/O逻辑，我们避免了重复工作，这样比在应用程序中分散处理好。

Thrift在Facebook的各种应用中得到了广泛应用，包括搜索、日志记录、移动应用、广告和开发者平台。我们发现，由额外的软件抽象层产生的边际性能成本远远被开发效率和系统可靠性的提升所掩盖。

## A. 类似的系统

下面是一些与Thrift类似的软件系统。每个都非常简略的描述：

- *SOAP*. XML-based. 专为通过HTTP提供web服务而设计，XML解析开销过大。
- *CORBA*. 使用相对广泛，由于过度设计和重度受到争议。同样笨重的软件安装。
- *COM*. 主要在Windows客户端软件被选择，不是一个开放的解决方案。
- *Pillar*. 轻量级和高性能，但是没有版本管理和抽象。
- *Protocol Buffers*. Google拥有的闭源产品，在Swazall论文中被描述。

## 致谢

Many thanks for feedback on Thrift (and extreme trial by fire) are due to Martin Smith, Karl Voskuil and Yishan Wong. Thrift is a successor to Pillar, a similar system developed by Adam D’Angelo, first while at Caltech and continued later at Facebook. Thrift simply would not have happened without Adam’s insights.

## References

1 Kempf, William, “Boost.Threads”, http://www.boost.org/doc/html/threads.html

2 Henkel, Philipp, “threadpool”, [http://threadpool.](http://threadpool/) [sourceforge.net](http://sourceforge.net/)
# proj279-NVMe-SSD-block-storage ｜ UNCE队

![Alt text](https://gitlab.eduxiji.net/T202410269992688/proj279-nvme-ssd-block-storage/-/raw/main/school_logo.png)


## 项目说明
- 赛道：2024 年全国大学生计算机系统能力大赛-操作系统设计赛(全国) —— OS 功能挑战赛道
- 赛题：proj279-NVMe-SSD-block-storage
- 赛题分类：2.3 操作系统内核大类-->2.3.6 内核完善
- 队名：UNCE
- 院校：华东师范大学
- \* 此文档包含初赛部分说明 *

## 项目概述
### 项目背景
- 《中国制造2025》指出今年来我国载人航天、大型飞机、北斗卫星导航等重大技术装备取得突破，这些设备都大量用到了实时系统和技术。近年来发展迅速的无人驾驶汽车、无人机、实时数据库等对块存储的实时性有了较高要求。同时，近年来新型存储硬件，如NVMe SSD快速发展，它们具有非易失性、抗冲击、低功耗和快速访问时间等特性，非常适合应用在实时块存储中。

- 由于无人驾驶算法、实时数据库等大都运行在linux操作系统上，所以本项目是在linux平台上增强以NVMe SSD为存储介质的块存储实时性。为此，需要设计相应的机制来提高linux块存储的实时性，比如根据进程的优先级将块存储IO进行分类的IO调度器，优先处理高优先级任务的IO请求，充分利用NVMe SSD的特性等。

- 由于无人驾驶算法、实时数据库等的块存储IO特点为：实时性要求较高的少量小文件读，同时可能存在实时性要求低的大量读写。为此，需要在保证实时性IO请求的前提下，尽量减少对块存储IO吞吐量的影响。

- 本项目的目的是鼓励对操作系统感兴趣的师生增强对linux内核的分析和改造能力，并将内核分析改造的方法和心得共享给大家。 这个项目的特点是：分析linux块存储IO栈，设计相应机制以提高liunx块存储的实时性，使得学生可以更好地理解和掌握linux的块存储子系统。

### 预期目标
- 分析Linux块存储IO栈，找出影响块存储实时性的主要几个因素，并进行修改优化；
- 设计实现可以根据进程优先级分类的IO调度器，在提高linux块存储IO实时性的前提下不过大影响块存储IO吞吐量。

### 仓库结构
```
.
├── README.md
├── Related_Paper_Analysis
│   └── Readme.md
├── blk_switch_analysis
│   └── README.md
├── block_layer_analysis
│   ├── README.md
│   ├── blk-mq
│   │   ├── README.md
│   │   ├── assets
│   │   │   ├── mq_get.png
│   │   │   ├── mq_init.png
│   │   │   ├── mq_make.png
│   │   │   ├── mq_make_insert.png
│   │   │   ├── mq_run_hw_q.png
│   │   │   └── mq_run_hw_q_p2.png
│   │   ├── blk_mqh.md
│   │   └── mq_call_stack.c
│   ├── block_layer
│   │   ├── README.md
│   │   ├── assets
│   │   │   ├── bio.png
│   │   │   └── block_position.png
│   │   └── block_layer_call_stack.c
│   └── nvme_driver
│       ├── README.md
│       └── aassets
│           ├── nvme_commands.png
│           └── nvmequeue.gif
├── result
│   ├── Readme.md
│   ├── error_msg.png
│   ├── error_msg2.png
│   ├── fig2.png
│   ├── fig3.png
│   ├── lapp.png
│   └── tapp.png
└── school_logo.png

10 directories, 28 files
```

## 第一阶段

### 当前内核环境
Linux内核（5.4.43版本）
### 初赛完成
1. 完成了对Linux操作系统块层的逻辑解读；
2. 针对“高吞吐、低延迟”的要求，组内参考了“blk-switch”技术，对原论文进行解读以及技术复现，同时据此对block层的multy queue结构进行了分析，梳理出了当前结构中存在的性能瓶颈；
3. 针对当前Linux中block层的架构，提出了一些实际的修改方向。

>Block Layer框架分析




>blk-switch解读


对于Linux操作系统块层的“高吞吐、低延迟”优化，我们参考了一篇名为[Rearchitecting Linux Storage Stack for μs Latency and High Throughput Jaehyun](https://www.usenix.org/conference/osdi21/presentation/hwang*) [1]的论文。其中，论文提出了一种称为“blk-switch”的技术，小组围绕此进行了相关的解读以及复现。

关于blk-switch，其最主要的思想就是在block层中，将存储设备初始化时就一一对应的软件队列与硬件队列解耦，使得当一个软件队列或者硬件队列拥塞时，可以进行类似网络中“交换”的操作，将接下来紧急的任务或者需要高吞吐量的任务重定位到较为空闲的队列中，从而在保证实时性IO请求的前提下，尽量减少对块存储IO吞吐量的影响。同时，也额外设置了“T-app”与“L-app”两种类型的标签，来对延时敏感型以及高吞吐需求的请求来加以区分。

通读论文，根据其中提供的开源代码仓库（参考仓库为[blk-switch: Rearchitecting Linux Storage Stack for μs Latency and High Throughput](https://github.com/resource-disaggregation/blk-switch)），我们对算法在代码层面的实现进行了分析，同时也据此对IO调度的性能瓶颈进行了跟踪。


注：此处列举代码的大体逻辑框架，[完整的代码]()以及[注释分析]()已放在blk-switch-analysis目录下。

在代码实现上，作者在5.4.43版本Linux内核的block层中，除去额外添加的blk-switch.h头文件，在blk-mq.c文件中添加了大约四百行的代码，实现了跨队列任务调度的功能。

对于算法的插入，主要在blk-mq.c文件中的blk_mq_get_request函数中进行了实现，在打包形成request之前，会对其所对应的

首先，是应用级的重定向。

算法会根据T类型以及L类型的字节数，计算出当前核上的负载，也同样，以此遍历合法的核，计算出负载最小的核，即为可用的目标核。
接下来，进行激活cpu以及设置亲和性等操作，将软件队列与硬件队列重新进行映射，即完成了对请求的重定向。

而若未进行应用级的重定向，算法就会切换到请求级别的重定向。程序会读取当前核的上下文以及请求数，若已经为低负载状态，就会在当前核上运行。否则，如同前者，也会进行关联核的遍历，依此对任务量以及活跃程度进行对比，选择负载较低的核作为目标切换核。而在存在远程访问的情况下，还会额外对比远程的队列负载。当负载均等无法判断时，程序则会进行随机选择，以保证队列整体的负载均衡。而在上述流程后依然未得到可切换目标核的话，程序就会随机挑选两个核，取其负载轻者作为目标。最后同样，对软件队列以及硬件队列进行更新、重定向。

最后，根据重定向后得到的data信息，构造出request，此时request便不再是进入原先的固定队列，而是进入经选择后的更优队列，从而完成块层调度的优化。

>论文结果复现
## 复现过程综述

我们对论文的结果进行了有限的复现，

### A.硬件配置

当前阶段

### B.过程

复现过程中，为了更加贴合项目的实际需求，同时抛去一些不必要的部分，组内并没有参照论文使用host与target两台主机，而只用了一台host服务器进行本地的测试。

小组主要修改了代码中的原硬盘访问路径，屏蔽了代码中关于远程网络访问磁盘的部分，在服务器上对T-app和L-app的blk-switch进行了部分复现（即仓库中Build blk-switch Kernel部分和Run Toy-experiments部分）。

####  1.编译构建blk-switch内核

对于blk-switch算法的植入，我们按照仓库的文档进行了如下配置：

* 以root身份进入主目录，下载并解压Linux内核源码；这里我们和仓库保持一致，使用linux-5.4.43.

* 克隆仓库并进入blk-switch的目录，并复制 blk-switch 源代码到内核源代码树。

* 进入内核源代码目录后更新内核配置，编辑 ".config" 文件，在文件中找到并修改CONFIG_LOCALVERSION.

* 确保内核配置中包含 i10 模块。

* 编译和安装内核、编译内核镜像和模块、安装内核模块和内核。

* 编辑 /etc/default/grub 文件，修改 GRUB_DEFAULT 行，更新 GRUB 配置以默认使用新内核启动，然后重启系统，检查当前内核版本，与文档一致。

#### 2.运行玩具实验脚本

##### 我们已经完成的：

参考运行结果：
1.`./toy_example_blk-switch.sh`
![error](https://github.com/hwts3041712310/proj279-nvme-ssd-block-storage/blob/main/result/lapp.png)
![error](https://github.com/hwts3041712310/proj279-nvme-ssd-block-storage/blob/main/result/tapp.png)

2.`Figure 2 (Single-core Linux): Increasing L-app load (5 mins):`
![error](https://github.com/hwts3041712310/proj279-nvme-ssd-block-storage/blob/main/result/fig2.png)

3.`Figure 3a (Single-core Linux): Increasing T-app I/O size (5 mins):`
![error](https://github.com/hwts3041712310/proj279-nvme-ssd-block-storage/blob/main/result/fig3.png)

可以看到，在本地的测试中，可以完成基本的L-app以及T-app测试，与论文结果基本一致。


- 1.遇到的问题 

在运行`https://github.com/resource-disaggregation/blk-switch/tree/master/osdi21_artifact `中测试的
`Figure 7: Increasing L-app load (6 mins)`

以及后续测试时，出现如下问题：

![error](https://github.com/hwts3041712310/proj279-nvme-ssd-block-storage/blob/main/result/error_msg.png)

- 2.可能的问题成因

由于1.部分的脚本可以正常运行，我们认为算法可以在单核的情况下顺利运行并测试。

查阅报错信息，小组发现程序在调用编号为8的cpu时找不到该编号的cpu，而本机的cpu编号只为0-5，所以无法次步无法顺利执行。


![error](https://github.com/hwts3041712310/proj279-nvme-ssd-block-storage/blob/main/result/error_msg2.png)

#### 3.已尝试过的方法：

修改调用的cpu的编号

- 主要修改了`$cpus`参数和`$nr_cpus`，将所有脚本文件中设置的`0，4，8，16，20`改为`0,1,2,3,4,5`，尝试只调用编号存在的cpu。

（一开始尝试过只修改`linux_fig7.pl`和`nr_lapp.pl`中的`0，4，8，16，20`调用编号，发现并不起作用。）

最终发现，这些改动并无效果，依旧报错，甚至可能出现连`Figure 2 (Single-core Linux): Increasing L-app load (5 mins):`和`Figure 3a (Single-core Linux): Increasing T-app I/O size (5 mins):`都无法运行的结果。



#### 4.More Thinking 

对于报错，小组在分析代码并咨询指导老师后认为，代码复现并无重大问题，而实验设备的质量有待提升。

>后续优化策略

在blk-switch原文中，为了实现区分出延迟敏感型应用与高吞吐需求类型的应用，增加了额外的L-app与T-app类型的优先级接口供用户设置。

在框架的修改上，代码相当于是对一一映射的软件队列与硬件队列进行了解耦操作，使得所有的请求都能找到更为高效的通道，从而被顺利调度执行。

而我们发现原文中对于L-app以及T-app的标签分类，是直接插入在原代码的优先级结构体中的。在此情况下，当队列中存在同LT类型应用的竞争时，任务就会失去优先级的调度可能。那么在此基础上，设置多级优先级可能会更加贴合实际、合理。其中的模糊处理，核心数需求设置以及阈值设计等等，我们可以根据实际需求而动态地设置，而更适应自己需要，更符合题目需求的工作场景。

同时，我们对request的生成阶段进行了干涉，而当request进入了队列，仍有可能因为不同请求的执行速度差异造成队列负载差异。对此，尚未有高效的调度方式[2]，而值得探索。

总体而言，当前的工作仅仅从调度方面优化了拥塞、实时性的问题，在此之上针对单队列还有很大的优化空间。

小组将在实现类似软件队列与硬件队列解耦的功能基础之上，考虑针对单队列的性能优化，以及处理在高负载的情况下更加合理的调度方式。





## 参考文献:
[1]Hwang J, Vuppalapati M, Peter S, et al. Rearchitecting linux storage stack for µs latency and high throughput[C]//15th {USENIX} Symposium on Operating Systems Design and Implementation ({OSDI} 21). 2021: 113-128.

[2]Didona D, Pfefferle J, Ioannou N, et al. Understanding modern storage APIs: a systematic study of libaio, SPDK, and io_uring[C]//Proceedings of the 15th ACM International Conference on Systems and Storage. 2022: 120-127.














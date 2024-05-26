# temp result during the work :bookmark_tabs:

Here stored the temp results during the replication work.Main procedure of how we actually replication the work has been moved to the front page in [Readme.md](Breadcrumbsproj279-nvme-ssd-block-storage/Readme.md).Check them there if you like.Anyway,I still left a copy in the readme file in this dir.View the report you like.


*************************************

>论文结果复现
## 复现过程综述

对论文的结果进行了有限的复现，

### A.硬件配置

当前阶段


![error](https://github.com/hwts3041712310/proj279-nvme-ssd-block-storage/blob/main/result/error_msg2.png)

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

对于报错，在分析代码并咨询指导老师后认为，代码复现并无重大问题，而实验设备的质量有待提升。

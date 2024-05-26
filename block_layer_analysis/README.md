# 块层代码分析

为了找出并优化改进块层中对实时性影响较大的因素，我们分析了`Linux5.4x`系统中IO子模块的代码。其中包括：对Linux块层（block layer）流程框架的分析、对Linux多队列块层实现（multi-queue）流程的分析以及对NVMe设备的特点以及相应的驱动程序的分析。
```
├── blk-mq
│   ├── README.md               //解读文档
│   ├── assets                  //图片文件
│   │   ├── mq_get.png
│   │   ├── mq_init.png
│   │   ├── mq_make.png
│   │   ├── mq_make_insert.png
│   │   ├── mq_run_hw_q.png
│   │   └── mq_run_hw_q_p2.png
│   ├── blk_mqh.md
│   └── mq_call_stack.c         //具体代码流程解读
├── block_layer
│   ├── README.md               //解读文档
│   ├── assets                  //图片文件
│   │   ├── bio.png
│   │   └── block_position.png
│   └── block_layer_call_stack.c    //具体代码流程解读
└── nvme_driver
    ├── README.md               //解读文档
    └── aassets                 //图片文件
        ├── nvme_commands.png
        └── nvmequeue.gif
```

## blk-mq

相应的数据结构分析以及流程分析（[blk-mq](./blk-mq/README.md)）。

Supportive:对应代码文件中blk_mq.h定义的相关函数及其作用位于([blk_mqh](./blk-mq/blk_mqh.md))

## block-layer

块层总体框架分析（[block-layer](./block_layer/README.md)）。

## nvme_driver

NVMe设备于驱动分析（[nvme](./nvme_driver/README.md)）。

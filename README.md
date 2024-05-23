# proj279-NVMe-SSD-block-storage ｜ UNCE队

![Alt text](https://gitlab.eduxiji.net/T202410269992688/proj279-nvme-ssd-block-storage/-/raw/main/school_logo.png)


## 项目说明：
- 赛道：2024 年全国大学生计算机系统能力大赛-操作系统设计赛(全国) —— OS 功能挑战赛道
- 赛题：proj279-NVMe-SSD-block-storage
- 赛题分类：2.3 操作系统内核大类-->2.3.6 内核完善
- 队名：UNCE
- 院校：华东师范大学
- \* 此文档包含初赛部分说明 *

## 项目概述：
### 项目背景：
- 《中国制造2025》指出今年来我国载人航天、大型飞机、北斗卫星导航等重大技术装备取得突破，这些设备都大量用到了实时系统和技术。近年来发展迅速的无人驾驶汽车、无人机、实时数据库等对块存储的实时性有了较高要求。同时，近年来新型存储硬件，如NVMe SSD快速发展，它们具有非易失性、抗冲击、低功耗和快速访问时间等特性，非常适合应用在实时块存储中。

- 由于无人驾驶算法、实时数据库等大都运行在linux操作系统上，所以本项目是在linux平台上增强以NVMe SSD为存储介质的块存储实时性。为此，需要设计相应的机制来提高linux块存储的实时性，比如根据进程的优先级将块存储IO进行分类的IO调度器，优先处理高优先级任务的IO请求，充分利用NVMe SSD的特性等。

- 由于无人驾驶算法、实时数据库等的块存储IO特点为：实时性要求较高的少量小文件读，同时可能存在实时性要求低的大量读写。为此，需要在保证实时性IO请求的前提下，尽量减少对块存储IO吞吐量的影响。

- 本项目的目的是鼓励对操作系统感兴趣的师生增强对linux内核的分析和改造能力，并将内核分析改造的方法和心得共享给大家。 这个项目的特点是：分析linux块存储IO栈，设计相应机制以提高liunx块存储的实时性，使得学生可以更好地理解和掌握linux的块存储子系统。

### 预期目标：
- 分析Linux块存储IO栈，找出影响块存储实时性的主要几个因素，并进行修改优化；
- 设计实现可以根据进程优先级分类的IO调度器，在提高linux块存储IO实时性的前提下不过大影响块存储IO吞吐量。


## 第一阶段

### 当前内核环境：
Linux内核（5.4.43版本）
### 初赛完成：
1. 完成了对Linux操作系统块层的逻辑解读；
2. 针对“高吞吐、低延迟”的要求，组内参考了“blk-switch”技术，对原论文进行解读以及技术复现，同时据此对block层的multy queue结构进行了分析，梳理出了当前结构中存在的性能瓶颈；
3. 针对当前Linux中block层的架构，提出了一些实际的修改方向。

>Block Layer框架分析




>blk-switch解读

对于“高吞吐、低延迟”，我们找到了一种高度契合此需求的技术：blk-switch技术，并尝试对该论文进行了解读与复现。

*论文来源：https://www.usenix.org/conference/osdi21/presentation/hwang*

关于blk-switch，其最主要的思想就是将block层中在存储设备初始化时就一一对应的软件队列与硬件队列进行解耦，使得一个软件队列或者硬件队列拥塞时，可以进行类似网络中“交换”的操作，将紧急的任务或者需要高吞吐量的任务重定位到较为空闲的队列中，从而在保证实时性IO请求的前提下，尽量减少对块存储IO吞吐量的影响。

在代码实现上，作者在5.4.43版本Linux内核的block层中，除去额外添加的blk-switch.h头文件，在blk-mq.c文件中添加了大约四百行的代码，实现了跨队列任务调度的功能。

### blk-mq.c文件

相比Linux5.4.43的原blk-mq.c文件，作者只对其中的三个函数进行了修改，分别是 blk_mq_get_request, blk_mq_make_request 以及 blk_mq_init_hctx. 
这里我们重点关注它们的修改部分。

#### 我们先关注修改较少的 blk_mq_make_request 以及 blk_mq_init_hctx.

blk_mq_make_request中，在调用blk_mq_get_request获取request之前，添加了
```
static blk_qc_t blk_mq_make_request(struct request_queue *q, struct bio *bio)
{
    //······
    //判断请求类型以及定义相关初始变量
    //······
    
	blk_qc_t cookie;//此函数的返回值

	/* blk-switch */
	blk_switch_set_ioprio(current, bio);

    //······
	//执行合并bio以及记录等相关操作
    //······

	rq_qos_throttle(q, bio);

	data.cmd_flags = bio->bi_opf;
	rq = blk_mq_get_request(q, bio, &data);// 从bio以及data中获取rq
	
    //······
	//请求的创建、清理与加塞等等
    //······

	return cookie;
}

```



```
static int blk_mq_init_hctx(struct request_queue *q,
		struct blk_mq_tag_set *set,
		struct blk_mq_hw_ctx *hctx, unsigned hctx_idx)
{
    //······
    //初始化硬件上下文
    //······

    //此处为blk-switch算法在blk_mq_hw_ctx结构体中额外添加的bool类型变量blk_switch，以表示任务blk-switch算法的支持与否
	/* blk-switch */
	hctx->blk_switch = 0;

    //······
    //一些初始化请求以及错误处理
    //······
}
```

```
static struct request *blk_mq_get_request(struct request_queue *q,
					  struct bio *bio,
					  struct blk_mq_alloc_data *data)
{
    //······
    //变量初始化
    //······

    //blk-switch算法相关变量初始化
	/* blk-switch variables */
	int nr_cpus, nr_nodes = num_online_nodes();
	bool req_steered, app_steered;
	req_steered = app_steered = false;// 尚未被重定向
	//确定核心数
	if (blk_switch_nr_cpus <= 0 ||
	   blk_switch_nr_cpus > num_online_cpus())
		nr_cpus = num_online_cpus();
	else
		nr_cpus = blk_switch_nr_cpus;

	//······
    //激活队列并获得上下文
    //······



    //一些变量的初始化以及调试信息
    //注意，此处的多种变量具体含义与作用并无明确介绍，因此我们更加注重算法本身的逻辑
	/*
	 * blk-switch: (1) reset variables, (2) print out statistics
	 *		if there's no traffic for 1000ms
	 */
	if (blk_switch_request(bio, data)) {
		if (blk_switch_reset_metrics == 0 ||
		   time_after(jiffies, blk_switch_reset_metrics)) {
			int i;

			for (i = 0; i < nr_cpus; i++) {
				blk_switch_T_bytes[i] = 0;
				blk_switch_L_bytes[i] = 0;
				blk_switch_T_metric[i] = 0;
				blk_switch_L_metric[i] = 0;
				blk_switch_T_appstr[i] = 0;
				blk_switch_L_appstr[i] = 0;
				blk_switch_stats_print[i] = 1;
			}

			blk_switch_last_appstr = 0;
			blk_switch_appstr_app = 0;
		}

		blk_switch_reset_metrics = jiffies
				+ msecs_to_jiffies(BLK_SWITCH_RESET_METRICS);

		if (blk_switch_debug && blk_switch_reset_metrics &&
		   blk_switch_stats_print[current->cpu]) {
			blk_switch_stats_gen[current->cpu] = 0;
			blk_switch_stats_str[current->cpu] = 0;
			blk_switch_stats_prc[current->cpu] = 0;
			blk_switch_stats_print[current->cpu] = 0;
		}
	}

    //根据环境中blk_switch_on变量的值，决定是否按照blk-switch的算法来进行调度。	
	/*
	 * blk-switch: Application Steering
	 */
	// blk_switch_on >= 2：进行应用级别的重定向。加上以下处理：
	if (blk_switch_on >= 2 && blk_switch_request(bio, data)) 
	{
		unsigned long L_core, T_core, min_metric;
		unsigned long iter_L_core, iter_T_core;
		int cur_cpu = data->ctx->cpu;
		int cur_node = data->hctx->numa_node;
		int i, sample = 0, iter_cpu, target_cpu = -1;

		/* 1-1. Update T_core */

		// 获取当前cpu上的T应用字节样本
		sample = blk_switch_T_bytes[cur_cpu];

		if (blk_switch_is_thru_request(bio)) {
			sample += bio->bi_iter.bi_size;		// 加上该请求长度

			if (blk_switch_T_metric[cur_cpu] == 0)// 若未初始化或为0
				blk_switch_T_metric[cur_cpu] = sample;// 变为sample
			else {
				blk_switch_T_metric[cur_cpu] -=
					(blk_switch_T_metric[cur_cpu] >> 3);
				blk_switch_T_metric[cur_cpu] +=
					(sample >> 3);
					// 可能是一种近似处理，尚未明确其具体含义
			}
		}
		if (blk_switch_T_metric[cur_cpu] < 10)
			blk_switch_T_metric[cur_cpu] = 0;

		/* 1-2. Update L_core */
		sample = blk_switch_L_bytes[cur_cpu];

		if (!blk_switch_is_thru_request(bio)) {
			sample += bio->bi_iter.bi_size;

			if (blk_switch_L_metric[cur_cpu] == 0)
				blk_switch_L_metric[cur_cpu] = sample;
			else {
				blk_switch_L_metric[cur_cpu] -=
					(blk_switch_L_metric[cur_cpu] >> 3);
				blk_switch_L_metric[cur_cpu] +=
					(sample >> 3);
			}
		}
		if (blk_switch_L_metric[cur_cpu] < 10)
			blk_switch_L_metric[cur_cpu] = 0;

		/* 2. Determine target_cpu every 10ms */

		// 若时间戳为0，加上10ms
		if (blk_switch_last_appstr == 0)
			blk_switch_last_appstr = jiffies +
				msecs_to_jiffies(BLK_SWITCH_APPSTR_INTERVAL);
		// 若超过了时间
		else if (time_after(jiffies, blk_switch_last_appstr)) {
			int i;

			for (i = 0; i < nr_cpus; i++) {
				blk_switch_T_appstr[i] = blk_switch_T_metric[i];
				blk_switch_L_appstr[i] = blk_switch_L_metric[i];
			}

			// 再次更新时间戳
			blk_switch_last_appstr = jiffies +
				msecs_to_jiffies(BLK_SWITCH_APPSTR_INTERVAL);

			// 切换处理应用
			if (blk_switch_appstr_app == BLK_SWITCH_T_APP)
				blk_switch_appstr_app = BLK_SWITCH_L_APP;
			else
				blk_switch_appstr_app = BLK_SWITCH_T_APP;
		}
		// 当前cpu上
		L_core = blk_switch_L_appstr[cur_cpu];
		T_core = blk_switch_T_appstr[cur_cpu];

		if (!blk_switch_is_thru_request(bio))
			min_metric = L_core + T_core;
		else
			min_metric = 8388608;

		// 循环遍历除了当前cpu之外的其它核心或节点
		for (i = 0; i < nr_cpus/nr_nodes; i++) 
		{
			iter_cpu = i * nr_nodes + cur_node;
			// 迭代的其它当前cpu上
			iter_L_core = blk_switch_L_appstr[iter_cpu];
			iter_T_core = blk_switch_T_appstr[iter_cpu];

			// 过滤不满足条件的cpu：当前核心与请发起核心相同，或者任务量已经超过了阈值
			if (iter_cpu == data->ctx->cpu ||
			   iter_L_core > BLK_SWITCH_THRESH_L)
				continue;

			/* Find target_cpu for L-apps */

			// 当前cpu环境就是L型且bio为Lapp
			if (blk_switch_appstr_app == BLK_SWITCH_L_APP &&
			   !blk_switch_is_thru_request(bio)) 
			   {
				// 满足切换条件：空间足够
				if (T_core && iter_L_core &&
				   (L_core + T_core) > (iter_L_core + iter_T_core) &&
				   min_metric > (iter_L_core + iter_T_core)) {
					target_cpu = iter_cpu;
					min_metric = iter_L_core + iter_T_core;
				}
			}
			/* Find target_cpu for T-apps */

			// 应用类型为T
			else if (blk_switch_appstr_app == BLK_SWITCH_T_APP &&
				blk_switch_is_thru_request(bio)) 
			{
				// T应用应当让延迟敏感的L应用先行
				// wait for L-app to move first	
				if (L_core < iter_L_core &&
				   iter_L_core < BLK_SWITCH_THRESH_L) 
				   // 当前cpu的L任务数小于其他cpu上的L任务数，且都不超过阈值，
				   // 则当前并不适合T应用跳转，直接退出。（因此在上面也是先判断L任务）
				{

					target_cpu = -1;
					break;
				}
				//否则，切换到较为空闲的cpu
				else if (L_core > iter_L_core &&
					min_metric > iter_T_core) {
					target_cpu = iter_cpu;
					min_metric = iter_T_core;
				}
			}
		}

		/* 3. Perform app-steering */
		// 具体切换操作
		if (target_cpu >= 0) 
		{
			struct cpumask *mask;
			mask = kcalloc(1, sizeof(struct cpumask), GFP_KERNEL);

			if (blk_switch_debug) {
				printk(KERN_ERR "(pid %d cpu %2d) %s app (%lu %lu) -> (%lu %lu) core %d",
					current->pid, current->cpu,
					IOPRIO_PRIO_CLASS(bio_prio(bio)) == BLK_SWITCH_L_APP ? "L":"T",
					L_core, T_core,
					blk_switch_L_appstr[target_cpu],
					blk_switch_T_appstr[target_cpu],
					target_cpu);
			}

			if (!blk_switch_is_thru_request(bio) &&
				atomic_read(&data->hctx->tags->active_queues) <= 1)
				blk_switch_L_metric[cur_cpu] = 0;
			else if (blk_switch_is_thru_request(bio) &&
				atomic_read(&data->hctx->tags->active_queues) <= 1)
				blk_switch_T_metric[cur_cpu] = 0;

			cpumask_clear(mask);
			cpumask_set_cpu(target_cpu, mask);// 将目标cpu设置为激活
			sched_setaffinity(current->pid, mask);// 设置当前进程亲和性
			kfree(mask);
		}

		if (current->cpu != cur_cpu) {
			// 软件队列与硬件队列映射
			data->ctx = per_cpu_ptr(q->queue_ctx, current->cpu);
			data->hctx = blk_mq_map_queue(q, data->cmd_flags, data->ctx);
			app_steered = true;// 表示已被重定向
			
		}
	}

	/*
	 * blk-switch: Request Steering for T-apps
	 */
	// blk_switch_on >= 1：1: +reqstr, 2: +appstr
	if (blk_switch_on >= 1 && blk_switch_request(bio, data) && 
	   blk_switch_is_thru_request(bio) &&
	   !app_steered && nr_cpus >= nr_nodes * 2) 	   
	   {
		// 未进行过app_steered，且cpu数不小于两倍节点数

		struct blk_mq_hw_ctx *iter_hctx;
		struct nvme_tcp_queue *driver_queue;
		int cur_cpu = data->ctx->cpu;
		int cur_node = data->hctx->numa_node;
		int i, two_cpu[2], two_nr[2], target_cpu = -1;
		int min_nr = 1024, min_active = 2048;
		int T_active, req_thresh;
		unsigned char two_rand[2];
		unsigned long L_metric;

		/* 1. Push T-requests into local queue until it becomes busy */
		iter_hctx = data->ctx->hctxs[HCTX_TYPE_DEFAULT];// 当前硬件默认上下文
		T_active = atomic_read(&iter_hctx->nr_active);// 原子读取上下文的活动请求数

		// 阈值设置
		if (blk_switch_thresh_B > 0)
			req_thresh = blk_switch_thresh_B * 2;
		else {
			if (data->hctx->blk_switch == BLK_SWITCH_TCP)
				req_thresh = BLK_SWITCH_TCP_BATCH * 2;// TCP切换情况下
			else
				req_thresh = BLK_SWITCH_TCP_BATCH / 2;// 非TCP
		}

		if (T_active <= req_thresh) {// 活动请求较小
			target_cpu = cur_cpu;// 切换到当前cpu
			goto req_steering;// 切换
		}
		else
			blk_switch_stats_str[cur_cpu]++;// 增加当前cpu统计量


		// 遍历相关联cpu
		for (i = 0; i < nr_cpus/nr_nodes; i++) {
			two_cpu[0] = i * nr_nodes + cur_node;
			iter_hctx = per_cpu_ptr(q->queue_ctx, two_cpu[0])->hctxs[HCTX_TYPE_DEFAULT];
			T_active = atomic_read(&iter_hctx->nr_active);
			L_metric = blk_switch_L_metric[two_cpu[0]];// 迭代cpu的L度量

			if (data->hctx->blk_switch == BLK_SWITCH_TCP) {
				driver_queue = iter_hctx->driver_data;
				two_nr[0] = BLK_SWITCH_TCP_BATCH -
						atomic_read(&driver_queue->nr_req);// 若使用tcp，获取队列为请求数
			}
			else
				two_nr[0] = 0;

			/* 2. Pick-up other queue considering i10 batching */
			if (!L_metric && (data->hctx->blk_switch == BLK_SWITCH_RDMA ||
			   (data->hctx->blk_switch == BLK_SWITCH_TCP &&
			   T_active < BLK_SWITCH_TCP_BATCH))) 
			{
				if (two_nr[0] < min_nr) // 当前任务量小于已知最小任务量
				{
					target_cpu = two_cpu[0];
					min_nr = two_nr[0];// 更新target与最小值
					min_active = T_active;// 活跃程度或待处理请求
				}
				//  2-1) considering #outstanding requests
				else if (two_nr[0] == min_nr) {// 相等
					if (T_active < min_active) {// 活跃程度
						target_cpu = two_cpu[0];
						min_active = T_active;
					}
					//  2-2) considering local queue
					else if (T_active == min_active) {
						if (two_cpu[0] == cur_cpu)
							target_cpu = two_cpu[0];
						//  2-3) randomly choose one among remainings
						else if (target_cpu != cur_cpu) {
							get_random_bytes(&two_rand[0], 1);
							two_rand[0] %= 2;
							if (two_rand[0] == 0)
								target_cpu = two_cpu[0];
						}
					}
				}
			}
		}

		/* 3. Otherwise, run power-of-two-choices among cores */
		if (target_cpu < 0) {
			get_random_bytes(&two_rand[0], 1);// 未找到合适的cpu
			two_rand[0] %= nr_cpus / nr_nodes;
			two_cpu[0] = two_rand[0] * nr_nodes + cur_node;// 随机但合法的cpu

			do {
				get_random_bytes(&two_rand[1], 1);
				two_rand[1] %= nr_cpus / nr_nodes;
				two_cpu[1] = two_rand[1] * nr_nodes + cur_node;
			} while(two_cpu[0] == two_cpu[1]);// 随机选择第二个不同的cpu

			// 比较两个cpu的负载，选择压力小的进行切换
			iter_hctx = per_cpu_ptr(q->queue_ctx, two_cpu[0])->hctxs[HCTX_TYPE_DEFAULT];
			two_nr[0] = atomic_read(&iter_hctx->nr_active);
			iter_hctx = per_cpu_ptr(q->queue_ctx, two_cpu[1])->hctxs[HCTX_TYPE_DEFAULT];
			two_nr[1] = atomic_read(&iter_hctx->nr_active);

			if (two_nr[0] <= two_nr[1])
				target_cpu = two_cpu[0];
			else
				target_cpu = two_cpu[1];
		}

req_steering:
		blk_switch_stats_gen[cur_cpu]++;// 产生增加
		if (cur_cpu != target_cpu) {
			blk_switch_stats_prc[target_cpu]++;// 处理次数增加
			data->ctx = per_cpu_ptr(q->queue_ctx, target_cpu);
			data->hctx = blk_mq_map_queue(q, data->cmd_flags, data->ctx);
			req_steered = true;// 切换到新队列
		}
	}
    //······
    //对刷新操作外的请求分配存在的调度器
    //······

	tag = blk_mq_get_tag(data);
	if (tag == BLK_MQ_TAG_FAIL) // 失败就清空之前的上下文
    {
		if (clear_ctx_on_error)
			data->ctx = NULL;
		blk_queue_exit(q);
		return NULL;
	}

	// 根据参数初始化新的rq对象
	rq = blk_mq_rq_ctx_init(data, tag, data->cmd_flags, alloc_time_ns);

	/* blk-switch: for output-port stats */
	if (data->hctx->blk_switch && bio && !req_steered) // 请求未被重定向
    {
		if (blk_switch_is_thru_request(bio))
			blk_switch_T_bytes[data->ctx->cpu] += bio->bi_iter.bi_size;
		else
			blk_switch_L_bytes[data->ctx->cpu] += bio->bi_iter.bi_size;
	}
	rq->steered = req_steered;

	//······
    //处理请求为刷新操作的情况
    //······
	data->hctx->queued++;// 已排队请求数增加
	return rq;
}
```
>论文结果复现
关于复现的操作，我们参考了论文中提供的开源仓库

**https://github.com/resource-disaggregation/blk-switch/tree/master**



>后续优化策略

在blk-switch原文中，为了实现区分出延迟敏感型应用与高吞吐需求类型的应用，增加了额外的L-app与T-app类型的优先级接口供用户设置。

在框架的修改上，代码相当于是对一一映射的软件队列与硬件队列进行了解耦操作，使得所有的请求都能找到更为高效的通道，从而被顺利调度执行。

而我们发现原文中对于L-app以及T-app的标签分类，是直接插入在原代码的优先级结构体中的。在此情况下，当队列中存在同LT类型应用的竞争时，任务就会失去优先级的调度可能。那么在此基础上，设置多级优先级可能会更加贴合实际、合理。其中的模糊处理，核心数需求设置以及阈值设计等等，我们可以根据实际需求而动态地设置，而更适应自己需要，更符合题目需求的工作场景。

总体而言，当前的工作仅仅从调度方面优化了拥塞、实时性的问题，在此之上针对单队列还有很大的优化空间。

小组将在实现类似软件队列与硬件队列解耦的功能基础之上，考虑针对单队列、性能优化









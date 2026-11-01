# RTP Stream 设计文档

## 1. 概述

RTP Stream 模块实现了基于 UDP 的 RTP 协议流接收能力，向上层提供统一的 `aic_stream` 接口（read/write/seek/tell/close/size），支持丢包重排序、历史回退寻址和零拷贝内存池管理。

### 1.1 设计目标

- 实现 `aic_stream` 接口，无缝接入中间件播放框架
- 支持 RTP 包乱序到达时的重排序（可配置开启/关闭）
- 支持有限的 backward seek（依赖缓存的历史包窗口）
- 通过内存池机制减少碎片、控制内存占用
- 单生产者（UDP 接收线程）、单消费者（上层 read 线程）模型

### 1.2 模块组成

```
rtp/
├── rtp_packet.h / .c       # RTP 包结构和内存池
├── rtp_sorter.h / .c       # RTP 包重排序器
├── aic_rtp_stream.h / .c   # RTP 流主体，实现 aic_stream 接口
└── doc/
    └── rtp_stream_design.md # 本文档
```

## 2. 架构总览

```
                          ┌──────────────────────┐
                          │   Upper Layer         │
                          │  (media_player etc.)  │
                          └──────────┬───────────┘
                                     │ aic_stream interface
                                     │ (read/write/seek/tell/close/size)
                          ┌──────────▼───────────┐
                          │    aic_rtp_stream     │
                          │  ┌─────────────────┐  │
                          │  │  Output List     │  │
                          │  │  (ordered,pkt)   │  │
                          │  │  with history    │  │
                          │  └────────▲────────┘  │
                          │           │            │
                          │  ┌────────┴────────┐  │
                          │  │  rtp_sorter     │  │  (可选, RTP_SORTER_ENABLE)
                          │  └────────▲────────┘  │
                          │           │            │
                          │  ┌────────┴────────┐  │
                          │  │ Download Thread │  │
                          │  │ (UDP recvfrom)  │  │
                          │  └─────────────────┘  │
                          └───────────────────────┘
                                      ▲
                                      │ UDP datagrams
                          ┌───────────┴───────────┐
                          │       Network          │
                          └────────────────────────┘
```

## 3. 核心数据结构

### 3.1 rtp_packet_t — RTP 包

```c
typedef struct rtp_packet {
    int payload_size;           // 有效载荷大小
    uint16_t seq;               // RTP 序列号
    uint32_t ssrc;              // 同步源标识
    struct rtp_packet *prev;    // 双向链表前驱
    struct rtp_packet *next;    // 双向链表后继
    char payload[];             // 柔性数组，存放 RTP 载荷
} rtp_packet_t;
```

**内存布局**：每个 packet 从内存池分配，大小为 `sizeof(rtp_packet_t) + POOL_PKT_PAYLOAD_MAX`。payload 作为柔性数组紧跟在结构体之后，实现一次分配。

### 3.2 rtp_sorter_t — 包重排序器

```c
/* Sorter cache 上限为 pool 总量的 1/3，留 2/3 给 output 两侧 */
#define RTP_SORTER_CACHE_MAX (POOL_PKT_COUNT / 3)  /* 156/3 = 52 */

typedef struct rtp_sorter {
    uint32_t ssrc;              /* Current tracked SSRC */
    uint16_t next_seq_out;      /* 期望输出的下一个序号 */

    rtp_packet_t *cache_head;   /* 排序缓存（循环双向链表） */
    rtp_packet_t *output_head;  /* 已排序输出队列 */
    int cache_size;             /* 缓存中的包数量 */
    int output_size;            /* 输出队列中的包数量 */
} rtp_sorter_t;
```

**排序算法**：
- 以 RTP sequence number 为键，在循环双向链表中按序插入
- `next_seq_out` 追踪期望输出的序号，连续序号到达时批量输出
- **固定上限**：当 `cache_size > 48` 时，强制跳过丢失的 seq，从 `cache_head->seq` 继续输出
- SSRC 变化时自动清空缓存重新开始

**与旧版差异**：
- 去掉 `k_min`/`k_max`/`max_sort_size` 三个动态窗口字段
- 不再使用 `k_min + cache_size` 动态放大上限（旧公式会导致 pool 枯竭）
- 改为 `RTP_SORTER_CACHE_MAX = POOL_PKT_COUNT / 3`，随 pool 大小自动缩放

### 3.3 aic_rtp_stream — RTP 流主体

```c
struct aic_rtp_stream {
    struct aic_stream base;     // 继承 aic_stream 接口

    /* UDP 接收 */
    char udp_buf[UDP_BUF_SIZE]; // 2KB 接收缓冲区
    int listenfd;               // UDP socket fd
    int port;                   // 监听端口

    /* Output List (有序包链表，带历史回溯) */
    rtp_packet_t *output_head;  // 最旧包（历史边界）
    rtp_packet_t *output_tail;  // 最新包
    rtp_packet_t *current_pkt;  // 当前消费位置的包
    int current_offset;         // 当前包内偏移
    int history_count;          // 历史包数量
    unsigned long history_bytes;// 历史数据总字节数
    int pending_count;          // 待消费包数量

    /* 线程同步 */
    pthread_t tid;
    atomic_int stop_flag;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    /* 可选排序器 */
    rtp_sorter_t *sorter;

    /* 文件位置追踪 */
    s64 file_pos;
    s64 file_size;
};
```

## 4. 关键流程

### 4.1 启动流程 (rtp_stream_open)

```
rtp_stream_open()
  │
  ├── mpp_alloc() 分配 aic_rtp_stream 结构体
  ├── rtp_packet_pool_init() 初始化全局内存池
  │     └── 预分配 POOL_PKT_COUNT 个 rtp_packet_t
  │         每个大小 = sizeof(rtp_packet_t) + 1500
  ├── rtp_packet_pool_add_recycle_cb(stream_recycle_packet, stream)
  ├── rtp_stream_parse_param() 解析 URL → port
  ├── pthread_mutex_init() / pthread_cond_init()
  ├── rtp_sorter_create()  [RTP_SORTER_ENABLE]
  │     └── rtp_packet_pool_add_recycle_cb(rtp_sorter_recycle, sorter)
  └── pthread_create() 启动下载线程
       └── rtp_download_thread()
```

### 4.2 数据接收流程 (rtp_download_thread)

```
rtp_download_thread()
  │
  ├── socket() + bind() UDP socket
  ├── setsockopt(SO_RCVTIMEO, 1s) 超时 1 秒
  │
  └── while (!stop_flag):
        ├── recvfrom() 接收 UDP 数据包
        │
        ├── [RTP_SORTER_ENABLE 路径]:
        │     ├── rtp_sorter_input()  插入排序缓存
        │     │     ├── rtp_packet_create_from_data() 解析 RTP 头
        │     │     │     如果 seq 有效 → 按序插入循环链表
        │     │     │     连续 seq → 批量移到 output 队列
        │     │     │     缓存超窗口 → 强制推进 seq
        │     │     └── 返回
        │     │
        │     └── rtp_sorter_get_pkt() 循环取已排序包
        │           └── output_list_append() 追加到输出链表
        │                 └── pthread_cond_signal() 唤醒消费者
        │
        └── [无排序器路径]:
              └── rtp_packet_create_from_data() → output_list_append()
```

### 4.3 数据读取流程 (rtp_stream_read)

```
rtp_stream_read(buf, len)
  │
  └── while (total < len):
        ├── pthread_mutex_lock()
        ├── 等待 current_pkt 可用 (pthread_cond_timedwait, 1s 超时)
        ├── 从 current_pkt 拷贝数据 (payload + current_offset)
        ├── 更新 current_offset / file_pos
        ├── 当前包耗尽 → output_list_advance() 推进到下一包
        │     ├── 旧包变为 history (history_count++, history_bytes+=)
        │     └── pending_count--
        └── pthread_mutex_unlock()
```

### 4.4 Seek 流程 (rtp_stream_seek)

```
rtp_stream_seek(offset, whence)
  │
  ├── 计算 new_pos (根据 SEEK_SET/CUR/END)
  │
  ├── Forward seek (new_pos > file_pos):
  │     └── 跳过当前包数据 → output_list_advance()
  │        直到到达目标位置
  │
  └── Backward seek (new_pos < file_pos):
        ├── 撤销当前包内偏移
        ├── 如果没有 current_pkt 但有历史:
        │     └── 恢复 output_tail 为 current_pkt
        ├── 沿 history 链表向旧方向回溯
        │     └── 逐包回退 history_count--, pending_count++
        └── 超出历史范围: 定位到最旧可用包的起点
```

**限制**：backward seek 只能回溯到历史窗口内的位置（即 `output_head` 到 `current_pkt` 之间的范围）。超出历史范围时定位到最旧可用包起点。

### 4.5 关闭流程 (rtp_stream_close)

```
rtp_stream_close()
  ├── atomic_store(&stop_flag, 1)
  ├── pthread_join() 等待下载线程退出
  ├── 清空 output list（所有包回收至内存池）
  ├── rtp_sorter_destroy()  [RTP_SORTER_ENABLE]
  ├── pthread_cond_destroy() / pthread_mutex_destroy()
  ├── free(url)
  └── mpp_free(stream)
```

## 5. 内存池设计

### 5.1 全局池

```c
#define RTP_POOL_MAX_RECYCLE_CBS 4

static struct {
    rtp_packet_t *freelist;        // 空闲链表头
    void *memory;                  // 预分配内存块基址
    pthread_mutex_t lock;
    struct {
        rtp_packet_recycle_fn fn;  // 回收回调
        void *arg;                 // 回调参数
    } recycle_cbs[4];              // 最多 4 个回收源
    int recycle_cb_count;
} g_pool;
```

### 5.2 低水位回收机制

当空闲包数 < `POOL_LOW_WATER` 时，**轮流调用所有已注册的回调**回收包：

```
pool_get()
  ├── 检测 freelist 数量
  ├── < POOL_LOW_WATER → 遍历 recycle_cbs[]
  │     ├── [0] stream_recycle_packet() → 回收 stream 最旧历史包
  │     └── [1] rtp_sorter_recycle()    → 回收 sorter 缓存最旧包
  └── 从 freelist 分配
```

三个链表（sorter cache、sorter output、stream output）共享全局 pool。当 pool 低水位时，stream 和 sorter 都可以吐回包。

### 5.3 大包降级

如果 RTP 载荷超过 `POOL_PKT_PAYLOAD_MAX`（1500 字节），则直接用 `malloc()` 分配，释放时通过地址范围判断走 `free()` 还是 `pool_put()`。

## 6. 配置与编译选项

| 宏 | 说明 |
|----|------|
| `RTP_SORTER_ENABLE` | 启用 RTP 包重排序功能 |
| `RTP_STREAM_DUMP_ENABLE` | 启用 RTP 数据 dump 到文件（调试用） |
| `RTP_SORTER_QUEUE_LOG_INTERVAL_MS` | 排序器队列状态日志间隔（毫秒），0 表示关闭 |

## 7. 线程模型

```
┌─────────────────┐         ┌─────────────────┐
│  Download Thread │         │  Consumer Thread │
│  (rtpstreamthd)  │         │  (player/decoder)│
└────────┬────────┘         └────────┬────────┘
         │                           │
         │  recvfrom() UDP           │  rtp_stream_read()
         │      │                    │       │
         │  rtp_sorter_input()       │  等待 cond_signal
         │  rtp_sorter_get_pkt()     │       │
         │      │                    │  从 current_pkt
         │  output_list_append() ────┤  拷贝数据
         │  (lock mutex)             │  output_list_advance()
         │  cond_signal() ──────────►│  (lock mutex)
         │                           │
```

- **生产者**：下载线程，recvfrom + 排序 + 追加到输出链表
- **消费者**：上层 read 调用线程
- **同步**：`pthread_mutex_t` + `pthread_cond_t`
- **停止**：`atomic_int stop_flag`，下载线程循环检查，关闭时先设标志再 join

## 8. URL 格式

```
rtp://<host>:<port>/<path>
```

示例：`rtp://192.168.1.100:5000/video`

解析仅提取端口号，host 和 path 目前忽略。

## 9. 局限性

- **无历史持久化**：backward seek 仅限内存中缓存的历史包，历史窗口大小取决于内存池容量和回收策略
- **无 RTCP 支持**：当前只接收 RTP 数据，不处理 RTCP 控制协议
- **无加密支持**：不支持 SRTP
- **单 SSRC**：排序器在 SSRC 变化时会清空缓存，不支持多路复用
- **端口固定**：bind 后端口不可变，不支持动态端口协商

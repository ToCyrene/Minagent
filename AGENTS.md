# Minagent

嵌入式 Debian 用极简 C 语言 LLM Agent。追求最小开销和极致性能，支持后台常驻。

## 快速参考

- **语言**：C11
- **构建**：`make`（动态链接）或 `make static`（独立二进制）
- **依赖**：libcurl（运行时），musl-gcc（静态编译用）
- **配置**：工作目录下的 `minagent.conf`，或通过 `-c <路径>` 指定

## 构建命令

```sh
# 标准构建（动态链接）
make
# 输出 bin/minagent

# 静态构建，适合嵌入式独立部署（零运行时依赖）
make static
# 输出 bin/minagent（静态链接）

# 清理
make clean
```

## 代码检查

暂无配置 lint 工具。使用：

```sh
gcc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -c src/*.c
```

## 架构概览

```
main.c ─── conf.c    （运行时配置文件解析）
  │
  ▼
agent.c ─── net.c    （libcurl HTTP，POST 到 OpenAI 兼容 API）
  │  │      json.c   （特化 OpenAI 格式 JSON 构建/提取，零动态分配）
  │  │      tools.c  （read_file / write_file / run_bash + 安全检查）
  │  │      utils.c  （字符串转义、trim 等）
  │  │
  │  ▼
  │  minagent.conf   （key=value 运行时配置）
  │
  ▼
  守护模式：Unix domain socket + fork(2) 每连接一个子进程
  交互模式：stdin/stdout 循环
```

### 文件布局

```
include/
  agent.h      对话循环、历史管理、工具调度
  net.h        curl_easy 封装的 HTTP POST 客户端
  json.h       特化 OpenAI JSON 构建器与响应提取器
  tools.h      三种工具 + 目录白名单 + 命令黑名单
  conf.h       运行时配置文件解析器（key=value）
  utils.h      字符串转义、trim、路径工具
src/
  main.c       入口、参数解析、守护进程启动
  agent.c      对话循环、历史管理、工具调度
  net.c         curl_easy 封装的 HTTP POST 客户端
  json.c        特化 OpenAI JSON 构建器与响应提取器
  tools.c       三种工具 + 目录白名单 + 命令黑名单
  conf.c        运行时配置文件解析器（key=value）
  utils.c       字符串转义、trim、路径工具
obj/          编译中间文件（.o）
bin/          最终可执行文件（minagent）
minagent.conf.example   带注释的配置模板
Makefile
```

### 头文件路径

所有头文件通过 `-Iinclude` 引入，源文件中使用 `#include "xxx.h"` 即可，无需添加目录前缀。

## 关键设计决策

### JSON 处理

使用**特化 OpenAI 格式提取器**，而非通用 JSON 解析器。API 接口：

- `json_build_request()` — 将消息数组 + 工具定义序列化为完整的
  `/v1/chat/completions` 请求体。使用单增长缓冲区和 `snprintf`。
  手动转义 `\n \r \t \\ \"`。

- `json_get_str()` / `json_get_int()` — 按点分隔路径从原始 JSON
  文本中提取值（如 `choices.0.message.content`）。单次遍历状态机
  实现，零堆分配。

- `json_extract_tool_calls()` — 从 assistant 消息中提取 `tool_calls`
  数组，返回每个调用的函数名和参数。

- `json_load_history()` / `json_save_history()` — 将消息数组以 JSON
  文件形式持久化，用于上下文恢复。

### 守护进程模式

- 创建 Unix domain socket（默认 `/tmp/minagent.sock`）。
- `fork()` + `setsid()` 进入后台。
- 每个连接 `fork()` 一个子进程，子进程以 socket 为标准输入输出
  运行交互循环。
- 信号处理：`SIGTERM`/`SIGINT` 保存历史并退出；`SIGHUP` 重新加载
  `minagent.conf`；`SIGCHLD` 回收子进程。
- 客户端通过 `nc -U /tmp/minagent.sock` 连接，无需专用客户端。

### 三种工具

Agent 提供以下三种工具供 LLM 调用，均以 OpenAI function calling 格式定义。

| 工具 | 参数 | 说明 |
|------|------|------|
| `read_file` | `path`（字符串） | 读取指定文件的全部内容，返回字符串。 |
| `write_file` | `path`（字符串）、`content`（字符串） | 将 content 写入指定文件，覆盖已有内容。 |
| `run_bash` | `command`（字符串） | 执行指定的 bash 命令，返回标准输出和标准错误。超时由 `timeout` 配置控制。 |

### 工具安全

- **目录白名单**（`allowed_dirs`）：read_file 和 write_file 执行前，
  先用 `realpath()` 解析目标路径，检查是否以任一白名单目录为前缀。
- **命令黑名单**（`banned_cmds`）：run_bash 执行前，提取命令的第一个
  词，检查是否在黑名单中。
- 工具执行有可配置的超时（`timeout` 配置项），基于 SIGALRM。

### 编译策略

默认编译器 `musl-gcc`，`-static` 静态链接。musl 静态二进制无运行时
库依赖，可直接复制到同架构的任意嵌入式 Debian 上运行。预估二进制
文件大小在 400KB 以内，输出到 `bin/minagent`。

### 配置

所有选项保存在运行时 `minagent.conf` 文件中（key=value 格式，`#`
开头为注释）。可通过命令行参数覆盖。示例如下：

| 配置键        | 默认值                                    | 命令行参数     |
|--------------|------------------------------------------|--------------|
| api_url      | http://localhost:8080/v1/chat/completions | -u / --url   |
| api_key      |（空）                                    | -k / --key   |
| model        | gpt-4                                    | -m / --model |
| history_file | agent_history.json                       | -H / --history|
| daemon       | false                                    | -d / --daemon|
| socket_path  | /tmp/minagent.sock                       | --socket     |
| allowed_dirs | /tmp                                     |（无参数）     |
| banned_cmds  | rm,dd,mkfs,shutdown,reboot...            |（无参数）     |
| timeout      | 30                                       |（无参数）     |
| max_turns    | 8                                        |（无参数）     |

## 交互命令

交互模式（非守护）下，识别以下命令：

| 命令                  | 操作                        |
|----------------------|-----------------------------|
| `/save [文件]`        | 保存历史到文件（默认使用配置值）  |
| `/load <文件>`        | 从文件加载历史                 |
| `/clear`             | 清空对话历史                  |
| `/exit` 或 `/quit`   | 退出 Agent                  |

## Agent 对话流程

```
agent_chat(用户输入):
  1. 追加 {role:"user", content:用户输入} 到 history[]
  2. 构建请求 JSON（messages + tool 定义）
  3. http_post() → 解析响应
  4a. 若 finish_reason=stop 且有 content：
        追加 assistant 消息，返回 content
  4b. 若 finish_reason=tool_calls：
        执行工具，安全检查
        追加 tool 结果消息
        跳回步骤 2（递增轮次计数）
  5.  轮次超过 max_turns → 强制结束并提示
```

## 代码风格

- C11，除 `_GNU_SOURCE`（popen、realpath 等需要）外不使用 GNU 扩展。
- 非必要的注释省略。
- 函数命名：`模块_动作_主语`（如 `json_get_str`、`tools_set_allowed_dirs`）。
- 除 libcurl 和 libc 外零外部依赖。
- 所有内存分配检查返回值；错误通过返回码传递，不使用 `assert()` 或 `abort()`。
- 缓冲区复用或 arena 分配；热路径无逐消息 malloc/free。

# Minagent 实现计划

## 依赖关系

```
agent  ←── json  ←── utils
  │              ←── tools ←── utils
  │              ←── net
main ←── agent
     ←── conf  ←── utils
     ←── tools
```

## Phase 1：基础层（无内部依赖）

### 1.1 `utils.c` — 字符串工具

- `utils_trim()` - 去除首尾空白
- `utils_escape_json()` - 转义 `\n \r \t \\ \"`
- `utils_split_csv()` - 按逗号分割字符串到数组

独立可测试，其余模块公用。

### 1.2 `net.c` — HTTP 客户端

- `http_init()` - 初始化 curl，设置 URL、API key、超时
- `http_post()` - POST JSON body，返回响应字符串
- `http_free()` - 清理

仅依赖 libcurl，可用真实或模拟接口验证。

## Phase 2：功能模块（依赖基础层）

### 2.1 `json.c` — OpenAI JSON 构建/提取

- `json_build_request()` - 消息数组 + 工具定义 → 请求体 JSON
- `json_get_str()` / `json_get_int()` - 按点分隔路径从响应中提取值
- `json_extract_tool_calls()` - 提取 tool_calls 数组
- `json_load_history()` / `json_save_history()` - 历史持久化

依赖 `agent.h`、`tools.h` 中的结构体定义（头文件已存在），可用静态 JSON 字符串独立测试。

### 2.2 `conf.c` — 运行时配置解析

- `conf_load()` - 逐行解析 key=value，处理注释、trim、列表切分
- `conf_print()` - 输出配置到日志
- `conf_free()` - 释放动态分配的内存

依赖 `utils`，可用示例配置文件独立测试。

### 2.3 `tools.c` — 三种工具

- `tool_read_file()` - 白名单校验 → `open/read`
- `tool_write_file()` - 白名单校验 → `open/write`
- `tool_run_bash()` - 黑名单校验 → `popen` + 超时控制
- `tools_set_allowed_dirs()` / `tools_set_banned_cmds()`

依赖 `utils`，每个工具可单独调用验证。

## Phase 3：核心编排（依赖 Phase 1 + 2）

### 3.1 `agent.c` — Agent 核心

- `agent_init()` - 初始化 HTTP、加载历史、加载工具定义
- `agent_chat()` - 主对话循环（用户输入 → 构建请求 → HTTP → 解析 → 工具调度 → 循环）
- `agent_load_history()` / `agent_save_history()` / `agent_clear_history()`
- `agent_free()`

串接 json → net → tools 形成完整对话闭环，完成后可端到端交互测试。

## Phase 4：入口集成（依赖全部）

### 4.1 `main.c` — 入口与守护进程

- 命令行参数解析（`-c -u -k -m -H -d`）
- 配置加载（`conf_load` → 命令行覆盖）
- 交互循环（stdin 模式，含 `/save /load /clear /exit`）
- 守护进程启动（`fork/setsid/socket/bind/listen/accept/fork`）
- 信号处理（SIGTERM/SIGINT/SIGHUP/SIGCHLD）

最后一步，绑定所有模块形成完整程序，完成后 `make static` 生成嵌入式部署用二进制。

## 提交策略

每完成一个 `.c` 文件的实现即提交一次，确保每次 commit 可编译通过。

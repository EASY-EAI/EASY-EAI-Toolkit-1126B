<br/>
<br/>


[English](README_EN.md)

<br />
<br />

如果您初次接触本项目，您可以：  
[查阅入门(快速上手)指南](https://www.easy-eai.com/document_details/27/1097)

如果您具备丰富的嵌入式C语言开发经验，您可以：  
[查阅EASYEAI开发实例](https://www.easy-eai.com/document_details/27/1150)  
[查阅API开发手册](https://www.easy-eai.com/document_details/27/1132)


使用方法：  
[1] - 克隆本GitHub仓库   
[2] - 进入具体的(demo/solution)目录执行：./build.sh   
[3] - 则会在具体的(demo/solution)目录的Release/下生成相关的可执行程序

重要更新：
---
> 2025-09-09 :
> * 发布 EASY-EAI-Toolkit-1126B-0.0.1
> * 更新 easyeai-api
>     * 通用组件api
>         * [移出] gst_opt
>         * [移出] http
>         * [更新] log_manager
>         * [移出] modbus
>         * [移出] rk_mpi
>     * 网络协议组件api
>         * [新增] http
>         * [新增] modbus
>     * 多媒体组件api
>         * [新增] gst_opt
>         * [新增] rk_mpi
>         * [新增] rga
> * 新增【Api单例】
>     * [重命名] netProtocol-http
>     * [重命名] netProtocol-modbus
> * 新增【开发实例】
>     * [新增] rtspMulitPlayer
>
> 2025-08-05 :
> * 发布 EASY-EAI-Toolkit-1126B-0.0.0
> * 更新 easyeai-api
>     * 通用组件api
>         * [新增] base64
>         * [新增] font_engine
>         * [新增] gst_opt
>         * [新增] http
>         * [新增] ini_wrapper
>         * [新增] ipc
>         * [新增] json
>         * [新增] log_manager
>         * [新增] modbus
>         * [新增] qrcode
>         * [新增] rk_mpi
>         * [新增] system_opt
> * 新增【Api单例】
>     * [新增] common-base64
>     * [新增] common-font_engine
>     * [新增] common-http
>     * [新增] common-ini
>     * [新增] common-modbus
>     * [新增] common-qrcode
>     * [新增] common-system_opt
> * 新增【开发实例】
>     * [新增] rtspIPCamera
>
> 2025-08-05 : 
> * 创建本项目
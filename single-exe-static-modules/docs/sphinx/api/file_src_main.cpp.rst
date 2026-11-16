
.. _file_src_main.cpp:

File main.cpp
=============

|exhale_lsh| :ref:`Parent directory <dir_src>` (``src``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS



Stacks_Tools 单入口：只负责运行时路径发现、CLI 路由与输出。 



.. contents:: Contents
   :local:
   :backlinks: none

Definition (``src/main.cpp``)
-----------------------------


.. toctree::
   :maxdepth: 1

   program_listing_file_src_main.cpp.rst



Detailed Description
--------------------

单 main 约束：

- 只 import 已导出的静态模块，不直接实现任务/路径/提醒业务；

- 启动时发现源码树或 Alfred workflow 根目录，并补齐 data/log 目录；

- CLI 参数只做轻量拆分，日期、优先级等规则全部下沉到 TaskManager；

- 所有命令统一返回 ok=0、usage=1、failed=2，供 Alfred 判断成功/失败音效。 






Includes
--------


- ``InterfaceLog`` (:ref:`file_modules_interface_log_include_InterfaceLog.cppm`)

- ``ProjectSync`` (:ref:`file_modules_project_sync_include_ProjectSync.cppm`)

- ``ReminderSync`` (:ref:`file_modules_reminder_sync_include_ReminderSync.cppm`)

- ``TaskManager`` (:ref:`file_modules_task_manager_include_TaskManager.cppm`)

- ``WorkflowUI`` (:ref:`file_modules_workflow_ui_include_WorkflowUI.cppm`)

- ``cstdio``

- ``nlohmann/json.hpp``

- ``std``

- ``unistd.h``






Namespaces
----------


- :ref:`namespace_anonymous_namespace{main.cpp}`


Classes
-------


- :ref:`exhale_struct_structanonymous__namespace_02main_8cpp_03_1_1_runtime`


Enums
-----


- :ref:`exhale_enum_namespaceanonymous__namespace_02main_8cpp_03_1a92908958d63fcd48339edb7291392e77`


Functions
---------


- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a3690dfe5829ec3653d33d6aeaf9601e7`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a2103b0ef481bcfc1e95ba676f208a783`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a606ce39a1755eec9dec2b1cff91db965`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a68daa168c27e1f6bc567079e22d9bf7d`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1af37ac4d3f616bef03b96e8bbe7a83ccd`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1ac96c816d3645dc7523aefae7bf224eb6`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1ab769e4cc706e1cc5387ec77115f58c80`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1aa67d382c269338acfcdc475fa70317a8`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1adbfcc7940dcfbc9f1a67b2e74567ff1d`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a1cf571cd7da9ab7d56f5c658e11962e8`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a973abfb2a70b044729b630090ee3ffbf`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a97adefb5e1161ee2ecf9a0ea5a3f8805`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a7fb02bd6b974aac93fbc769bf26c2d34`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a5458b791af3328611d961315ea3d6af4`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1aeae47f485bca3545473aa4b8a0cb0060`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1ae4b477a6f2152597788185b009998b74`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a5a52ff23f36b859f4316fa4bcec0cae8`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a7d8e8699a4ae994df1cc5fb14e570b72`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a6520f81923b9670964b859bce0810e2b`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a924c6a1c7f3f745cf4b7ca3a28dce219`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1af4e217736fdf4f9b8455eedb8910e55e`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a121688d8cd168f46ee5a1da6369f8460`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1ab8cd395acf228094697027eb26a882ff`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1ade68368a2473781b5c776f9247b2aeb2`

- :ref:`exhale_function_namespaceanonymous__namespace_02main_8cpp_03_1a8994974c767158c52b9f92aa44a68e0c`

- :ref:`exhale_function_main_8cpp_1a0ddf1224851353fc92bfbff6f499fa97`


set_project("zFileTransfer")
set_version("1.0.0")

set_languages("cxx17", "c11")

add_rules("mode.debug", "mode.release")

add_requires("spdlog 1.14.1", {system = false})

-- 这行代码会让 Xmake 自动去读取并编译 submodules/minirtc 目录下的 xmake.lua
includes("submodules/minirtc")

target("zFileTransfer")
  set_kind("binary")

  add_includedirs("src", "src/log")

  -- 自动递归添加 src 目录下的所有 .cpp 和 .c 源文件
  add_files("src/**.cpp", "src/**.c")

  -- 关键：添加对 minirtc 的依赖
  -- 注意：这里的 "minirtc" 必须与 submodules/minirtc/xmake.lua 内部定义的 target 名称完全一致
  add_deps("minirtc")

  add_packages("spdlog")

  -- 编译选项优化（Debug 模式开启调试，Release 模式开启极限优化）
  if is_mode("debug") then
      set_symbols("debug")
      set_optimize("none")
  elseif is_mode("release") then
      set_symbols("hidden")
      set_optimize("fastest")
      set_strip("all")
  end

  -- 针对 Linux 环境的系统库链接（网络和多线程项目通常需要）
  if is_plat("linux") then
      add_syslinks("pthread", "m", "dl")
  end

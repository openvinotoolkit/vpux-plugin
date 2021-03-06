# OpenVINO VPUX Plugins family

## Components in this repository

* VPUX NN Compiler
  * [Build and Test Instructions](src/vpux_compiler/docs/build_and_test.md)
  * [Debugging Techniques](src/vpux_compiler/docs/debugging.md)
  * [Software layer enabling steps](/src/vpux_compiler/docs/sw_layer_enabling.md)
* VPUX Plugin
  * VPUAL Backend
  * Zero Backend
  * HDDL2 Backend
* [Software Kernels Implementation](./sw_runtime_kernels/README.md)

## Documentation

### Prerequisites

* Doxygen

    ```bash
    sudo apt install doxygen
    ```

* OpenJDK (for plantuml diagrams)

    ```bash
    sudo apt install default-jdk
    ```

### How to generate

VPUX Plugin has automatically generated documentation describing the plugin design and API used for implementing it.
Documentation can be built by the following commands

```bash
cmake -DENABLE_DOCS=ON ..
make vpux_plugin_docs
```

Execute the following to open documentation

```bash
chromium-browser ./docs/VPUX_DG/generated/html/index.html # chromium users
firefox ./docs/VPUX_DG/generated/html/index.html # firefox users
```

## = Environment =

### Git projects

The following projects are used and must be cloned including git submodules update:

* [OpenVINO Project]
* [KMB Plugin Project]

### Environment variables

The following environment variables should be set:

* The `OPENVINO_HOME` environment variable to the [OpenVINO Project] cloned directory.
* The `KMB_PLUGIN_HOME` environment variable to the [KMB Plugin Project] cloned directory.

### KMB Environment variables

The testing command assumes that the KMB board was setup and is available via ssh.

The following environment variables should be set:

* The `KMB_BOARD_HOST` environment variable to the hostname or ip address of the KMB board.
* The `KMB_WORK_DIR` environment variable to the working directory on the KMB board.

## = Build =

* [How to build VPUX Plugin](guides/how-to-build.md)
* [How to build and use custom vpualHost](guides/how-to-build-vpualHost.md)

## = Run =

* [How to deploy VPUX Plugin build to KMB board](guides/how-to-deploy.md)
* [How to run tests on KMB board](guides/how-to-test.md)
* [How to set devices and platforms](guides/how-to-set-devices-and-platforms.md)

### Bypass mode (HDDL2)

Bypass related preparations

* [How to setup KMB bypass](guides/how-to-use-kmb-bypass.md)
* [How to setup TBH bypass](guides/how-to-use-tbh-bypass.md)

### [MCM Emulator]

## = Development =

### ClangFormat

* Install the proper version of ClangFormat tool:
  * In Linux: `$ sudo apt install -y clang-format-9`
  * If you use another OS, please update this document after you install the application
* Set up the formatter tool in your IDE:
  * For Visual Studio Code IDE, follow [this manual](https://code.visualstudio.com/docs/cpp/cpp-ide#_code-formatting).
    * In Linux, the path to ClangFormat9 application is usually `/usr/bin/clang-format-9`
      You can check it with `$ which clang-format-9`
  * If you use another IDE, please update this document after you set it up properly
* If necessary, you can format the whole project at once:
  * Set CMake option: `-D CLANG_FORMAT=path_to_your_clang_format_9`
  * Build target `clang_format_fix_all`

### Code style

* [VPUX Compiler C++ code-style document](src/vpux_compiler/docs/code_style.md)

### Developer build

The VPUX plugin has extra CMake option to enable Developer build, which is orthogonal mode for Release/Debug configuration.
The mode is enabled with `-D ENABLE_DEVELOPER_BUILD=ON` CMake option, which should be added to kmb-plugin CMake command line.
The mode enables extra debugging and logging functionality not available in default build:

* Pipeprint functionality on KMB board. It allows to get logs from VPU side on ARM.
  Can be enabled with `IE_VPUX_ENABLE_PIPEPRINT=1` environment variable.

### Debugging - Getting output from runtime

The following environment variables should be set:

* The `TOOLS_DIR` environment variable to the Movidius Tools directory.
* The `VPUIP_HOME` environment variable to the [VPUIP_2 Project] cloned directory

1. Build firmware via `make_std_fw_image.py` with options:
    * CONFIG_USE_COMPONENT_PIPEPRINT='y'
    * CONFIG_USE_SHAVE_PIPEPRINT='y'
2. `rsync -avz $VPUIP_HOME/application/vpuFirmware/vpu_b0.bin root@$KMB_BOARD_HOST:/lib/firmware/vpu_custom.bin`
3. Start server

    ```bash
    cd $TOOLS_DIR/linux64/bin
    ./moviDebugServer --arm-reset=none
    ```

4. Start Movidius debug tool

    ```bash
    cd $VPUIP_HOME/application/vpuFirmware/FW_bootLoader
    make debugi
    ```

5. Run the app on the device, the logs will be displayed via moviDebug2

### Profiling - enable performance measuring in compiler

To enable insertion of additional timer DMAs in the network:

1. Compile and run inference using benchmark_app with additional flag "-pc" (For using existing blob see 2.)
2. To compile blob (using compile_tool) with profiling enabled:
    a) Enable it in the compiler config file(`*.json`): look for "PerformanceCounting" in "GlobalConfigParams"
    b) Add "PERF_COUNT YES" to compiler config file (-c option in compile_tool)
3. To parse profiling data manually you can use prof_parser. Run prof_parser -h to get info about its usage.

## === Integration ===

### How to update graph schema in mcmCompiler

To update generated C++ headers for graph schema add the following parameter to kmb-plugin CMake configure command: `-D MCM_GRAPH_SCHEMA_TAG=<tag or branch name>`, where `<tag or branch name>` should be an existing tag or branch in `graphFile-schema` repository.

It will add graph schema update target to the common build. The C++ headers for graph schema will be updated during the build.

**Note:** The generated headers are stored in the [KMB Plugin Project] repository and must be committed if there are changes. This is done to simplify cross-compilation build and build without access to `graphFile-schema` repository.

### How to port changes from mcmCompiler GitHub

To port changes from `mcmCompiler` GitHub repository to kmb-plugin run the following commands:

```bash
export MCM_PATCH_FILE=~/mcm.patch
cd $MCM_COMPILER_HOME
git diff [first commit]..[last commit] > $MCM_PATCH_FILE
cd $KMB_PLUGIN_HOME
git apply --directory=src/mcmCompiler/ --reject $MCM_PATCH_FILE
```

Where `[first commit]..[last commit]` - commit range to transfer. For example, `[first commit]` is previous merge commit, `[last commit]` - current merge commit for PR.

The above commands will transfer code difference to kmb-plugin repository. Separate commit still should be created.

`git diff` / `git apply` can be replaced with `git format-patch` / `git am` to transfer separate commits with their messages and other properties. See git documentation for details.

### How to integrate vpualHost to kmb-plugin

```bash
export KMB_PLUGIN_HOME=<path to kmb-plugin>
export VPUAL_HOME=<path to vpualHost>
rm -rf $KMB_PLUGIN_HOME/artifacts/vpualHostInstallPackage/*
cp -r $VPUAL_HOME/install/* $KMB_PLUGIN_HOME/artifacts/vpualHostInstallPackage/
git checkout -b <name_of_new_branch>
git add -A
git commit -m "integrate new version vpualHost"
```

## === Dependencies ===

### G-API Preprocessing

The VPUX plugins uses G-API based preprocessing located in **G-API-VPU project**.

For any questions regarding this component please refer to **G-API-VPU project** maintainers.

[OpenVINO Project]: https://github.com/openvinotoolkit/openvino
[KMB Plugin Project]: https://github.com/openvinotoolkit/vpux-plugin

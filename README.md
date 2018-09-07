# REAMP Chromium
This is the fork of chromium used by the REMAP team to create an AR-Browser.
## Build Process
The process of building this project is very much like the process of building chromium for linux (https://chromium.googlesource.com/chromium/src/+/master/docs/linux_build_instructions.md):

### 1. Install depot_tools
Run the following commands in the terminal:
```
$ git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
```
After running the command above you need to add depot_tools to your path (**Note: the path to depot_tools should always be included in you $PATH when you try to build chromium so if you want to build it from time to time it is better to add it permanently**):
* **Adding depot_tools temporarily**: Run the following command:
```
$ export PATH=$PATH:/path/to/depot_tools
```
* **Adding depot_tools permanently**: Add the following line to the ~/.bashrc file:
```
export PATH=$PATH:/path/to/depot_tools
```
  and run the following command:
```
$ source ~/.bashrc
```
### 2. Get the code
Create a directory like chromium/src:
```
$ mkdir chromium && cd chromium
```

and clone the code into it(**Note: Based on your Internet may take some time as it's quite a large project**):
```
$ git clone https://github.com/remap/chromium.git src
```
**Note: All the command from here are related to the chromium directory**
### 3. Run the hooks
To install the BUILD_DEPS run the following command:
```
$ gclient runhooks
```
### 4. Setting up the build
Generate the .ninja files through the following command:
```
gn gen out/Default
```
### 5. Build Chromium
Run the following command:
```
$ autoninja -C out/Default chrome
```
### 6. Run Chromium
Run the following command:
```
$ out/Default/chrome
```
## List of functionalities added in this fork
  * Hello class and say hello functionality
## Hello class and say hello functionality
This part is for those new to chromium and explains the exact steps to add a functionality to the chromium and expose it to the html files.
### Writing the "Hello World!" Code
You should go through the follwing steps:
create a directory name hello in the ```chromium/src/third_party/blink/renderer/core/``` directory, then you need to create the c++(hello.cc) and header file(hello.h) as the ones in the ```chromium/src/third_party/blink/renderer/core/hello/``` and also create a BUILD.gn file for building these two files in the same directory. (**Note: If you don't know how to create .gn files take a look at this link: https://chromium.googlesource.com/chromium/src/tools/gn/+/48062805e19b4697c5fbd926dc649c78b6aaa138/README.md**)
### Adding a reference to the code to appropriate BUILD.gn files
For adding your code to the chromium's build process you should add the path to the BUILD file for your code to the appropriate BUILD files of chromium which in this case is ```chromium/src/third_party/blink/renderer/core/BUILD.gn``` so the line ```"//third_party/blink/renderer/core/hello"``` is added to it.
By adding the directory to your code to one of the chromium's BUILD files your code will be built in the process of building the chromium.
### Exposing the functionality
The next step is exposing your function to html files, for this paper you should write a .idl file in your directory like that in the ```chromium/src/third_party/blink/renderer/core/hello/``` (**Note: If you don't know how to write a .idl file take a look at this link: https://www.codeproject.com/Articles/19605/Basics-of-an-IDL-file and the .idl file that exists in the hello directory of this project and it may help you understand the whole concept**).
After creating the .idl file you should give a reference to it in the appropriate .gni file as well which in this case is ```chromium/src/third_party/blink/renderer/core/core_idl_files.gni``` so the line ```"hello/hello.idl"``` is added to this file.
**Note: Till here if any changes is made in any of the stated files you should again run the command related to building the chromium (the autoninja command).**
### Write layout test files
After you exposed your code you can test it by creating test html files in the ```chromium/third_party/WebKit/LayoutTests/external/wpt/``` so in this case we create a hello directory and write a html test file as the one in the ```chromium/third_party/WebKit/LayoutTests/external/wpt/hello/``` directory and then run the layout test. (**Note: If you don't know how to run all or some of the layout tests take a look at this link: https://chromium.googlesource.com/chromium/src/+/master/docs/testing/layout_tests.md**)
**Note: There is no need to re-build the project if you changed the test files.**


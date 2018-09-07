# REAMP Chromium
This is the fork of chromium used by the REMAP team to create an AR-Browser.
## Build Process
The process of building this project is very much like the process of building chromium for linux (https://chromium.googlesource.com/chromium/src/+/master/docs/linux_build_instructions.md):
1. Install depot_tools
2. Get the code
3. Run the hooks
4. Setting up the build
5. Build Chromium
6. Run Chromium

### Install depot_tools
Run the following commands in the terminal:
```
$ git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
```
After running the command above you need to add depot_tools to your path (**Note: the path to depot_tools should always be included in you $PATH when you try to build chromium so if you want to build it from time to time it is better to add it permanently**):
* **Adding depot_tools temporarily**: Run the following command:
```
$ export PATH="$PATH:/path/to/depot_tools"
```
* **Adding depot_tools permanently**: Add the following line to the ~/.bashrc file:
```
export PATH="$PATH:/path/to/depot_tools
```
  and run the following command:
```
$ source ~/.bashrc
```
### Get the code
Create a directory like chromium/src:
```
$ mkdir chromium && mkdir chromium/src && cd chromium/src
```
**Note: All the command from here are related to the chromium/src directory**
and clone the code into it:
```
$ git clone https://github.com/remap/chromium.git
```
### Run the hooks
To install the BUILD_DEPS run the following command:
```
$ gclient runhooks
```
### Setting up the build
Generate the .ninja files through the following command:
```
gn gen out/Default
```
### Build Chromium
Run the following command:
```
$ autoninja -C out/Default chrome
```
### Run Chromium
Run the following command:
```
$ out/Default/chrome
```
## List of functionalities added in this fork
  * Hello class and say hello functionality
##

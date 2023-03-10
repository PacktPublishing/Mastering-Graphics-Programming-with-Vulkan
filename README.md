# Mastering Graphics Programming with Vulkan

<a href="https://www.packtpub.com/product/mastering-graphics-programming-with-vulkan/9781803244792?utm_source=github&utm_medium=repository&utm_campaign=9781803244792"><img src="https://static.packt-cdn.com/products/9781803244792/cover/smaller" alt="" height="256px" align="right"></a>

This is the code repository for [Mastering Graphics Programming with Vulkan](https://www.packtpub.com/product/mastering-graphics-programming-with-vulkan/9781803244792?utm_source=github&utm_medium=repository&utm_campaign=9781803244792), published by Packt.

**Develop a modern rendering engine from first principles to state-of-the-art techniques**

## What is this book about?
Vulkan is now an established and flexible multi-platform graphics API. It has been adopted in many industries, including game development, medical imaging, movie productions, and media playback. Learning Vulkan is a foundational step to understanding how a modern graphics API works, both on desktop and mobile.

This book covers the following exciting features:
* Understand resources management and modern bindless techniques
* Get comfortable with how a frame graph works and know its  advantages
* Explore how to render efficiently with many light sources
* Discover how to integrate variable rate shading
* Understand the benefits and limitations of temporal anti-aliasing
* Get to grips with how GPU-driven rendering works
* Explore and leverage ray tracing to improve render quality

If you feel this book is for you, get your [copy](https://www.amazon.com/dp/1803244798) today!

<a href="https://www.packtpub.com/?utm_source=github&utm_medium=banner&utm_campaign=GitHubBanner"><img src="https://raw.githubusercontent.com/PacktPublishing/GitHub/master/GitHub.png"
alt="https://www.packtpub.com/" border="5" /></a>

## Instructions and Navigations
All of the code is organized into folders. For example, chapter2.

The code has been tested with the following software:
- Visual Studio 2019 Community Edition 16.11.8 (Windows)
- gcc 9 (Linux)
- CMake 3.22.1
- Vulkan SDK 1.2.198.1 or above
- SDL version 2.0.18
- assimp 5.2.2

### Getting the code
This repository includes a submodule that makes it easier to get glTF models. To make sure the submodule is initialized properly, run the following command when cloning the repository:
`git clone --recurse-submodules https://github.com/PacktPublishing/Mastering-Graphics-Programming-with-Vulkan`

To download the glTF assets using the bootstrap script, run the following command: `python ./bootstrap.py`

Alternatively, you get manually download the models from https://github.com/KhronosGroup/glTF-Sample-Models. We tested only only a subset of glTF 2.0 models.

### Windows
We provide a Visual Studio solution containing the code for all chapters, located at `project\RaptorEngine.sln`.

### Linux
We provide the assimp library as part of this repo, while the SDL library has to be installed manually. On Debian and Ubuntu this can be done as follows:
`sudo apt install libsdl2-dev`

Assuming you unpacked the Vulkan SDK in `~/vulkan/1.2.198.1`, you have to add the following lines to your `.bashrc` file:
```
export VULKAN_SDK=~/vulkan/1.2.198.1/x86_64
export PATH=$VULKAN_SDK/bin:$PATH
export LD_LIBRARY_PATH=$VULKAN_SDK/lib:$LD_LIBRARY_PATH
export VK_LAYER_PATH=$VULKAN_SDK/etc/vulkan/explicit_layer.d
```

To generate the Make file, run the following command:
`cmake -B build -DCMAKE_BUILD_TYPE=Debug`

To build a given chapter, run the following command:
`cmake --build build --target Chapter1 -- -j 4`

**Following is what you need for this book:**
This book is for professional graphics and game developers who want to gain in-depth knowledge about how to write a modern and performant rendering engine in Vulkan. Familiarity with basic concepts of graphics programming (i.e. matrices, vectors, etc.) and fundamental knowledge of Vulkan are required.

With the following software and hardware list you can run all code files present in the book (Chapter 1-15).
### Software and Hardware List
| Chapter | Software required | OS required |
| -------- | ------------------------------------ | ----------------------------------- |
| 1-15 | Vulkan 1.2 | Windows or Linux |

We also provide a PDF file that has color images of the screenshots/diagrams used in this book. [Click here to download it](https://packt.link/ht2jV).

### Related products
* 3D Graphics Rendering Cookbook [[Packt]](https://www.packtpub.com/product/3d-graphics-rendering-cookbook/9781838986193?utm_source=github&utm_medium=repository&utm_campaign=9781838986193) [[Amazon]](https://www.amazon.com/dp/1838986197)

* Vulkan Cookbook [[Packt]](https://www.packtpub.com/product/vulkan-cookbook/9781786468154?utm_source=github&utm_medium=repository&utm_campaign=9781786468154) [[Amazon]](https://www.amazon.com/dp/1786468158)

## Errata 
 * Page 6 (Almost at the end of the page):  **$ cmake --build build --target chapter1 -- -j 4** _should be_ **$ cmake --build build --target Chapter1 -- -j 4**

## Get to Know the Authors
**Marco Castorina** first got familiar with Vulkan while working as a driver developer at Samsung. Later he developed a 2D and 3D renderer in Vulkan from scratch for a leading media-server company. He recently joined the games graphics performance team at AMD. In his spare time, he keeps up to date with the latest techniques in real-time graphics. He also likes cooking and playing
guitar.

**Gabriel Sassone** is a rendering enthusiast currently working as a Principal Rendering Engineer at Multiplayer Group. Previously working for Avalanche Studios, where his first contact with Vulkan happened, where they developed the Vulkan layer for the proprietary Apex Engine and its Google Stadia Port. He previously worked at ReadyAtDawn, Codemasters, FrameStudios, and some non-gaming tech companies. His spare time is filled with music and rendering, gaming, and outdoor activities.

### Download a free PDF

 <i>If you have already purchased a print or Kindle version of this book, you can get a DRM-free PDF version at no cost.<br>Simply click on the link to claim your free PDF.</i>
<p align="center"> <a href="https://packt.link/free-ebook/9781803244792">https://packt.link/free-ebook/9781803244792 </a> </p>

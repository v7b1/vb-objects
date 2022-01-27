# vb-objects
This is a slowly growing collection of externals made for the programming environment MaxMSP (www.Cycling74.com).



## Building

### Mac

You can build on the command line using Makefiles, or you can generate an Xcode project and use the GUI to build.

- Xcode:

  ```
  cmake -G Xcode ..
  cmake --build . --config 'Release'
  ```

  or, instead of the second step, open the Xcode project and use the GUI.

- Make:

  ```
  cmake ..
  cmake --build . --config 'Release'
  ```

  Note that the Xcode project is preferrable because it is able to substitute values for e.g. the Info.plist files in your builds.



### Windows

Note: this is untested, but should work something like this:

```
cmake ..
cmake --build . --config 'Release'
```



Compiled objects for osx can be found here: https://vboehm.net/downloads

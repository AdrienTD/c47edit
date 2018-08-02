# c47edit - Scene editor Hitman C47

Version 0.0.1

By AdrienTD

## Introduction

c47edit is a tool where you can **view and edit scenes/levels** in **Hitman Codename 47**.
You can for example move objects, and duplicate them.

But c47edit is just **in development**, and what you can do with it is really
**limited**. The game might also **crash** when you do certain modifications.

c47edit requires **Windows (XP or more recent versions)** as well as an **OpenGL driver**.

c47edit is also open source, licensed under the GPL 3.
You can get the source code from [GitHub](https://github.com/AdrienTD/c47edit).

## Start

When you **run c47edit.exe**, a file dialog box will show up. Select a **ZIP file** containing
a scene (e.g. <game-folder>\C1_HongKong\C1_1.zip). It is not a bad idea to make a
**backup** of the file before.

After opening a file, the editor window will appear. You should see a red background
and 3 windows: **Object tree**, **Object information**, and **c47edit**.

In the **Object tree** window, you can see the whole scene graph including all objects
present in the scene. There are also group objects which can contain other objects
inside them. You can expand or collapse group objects by **clicking the arrow** or by
**double-clicking** it.

Note that, by default, the level is not rendered. You first have to mark an object
as viewable.
If you want to render the whole level, hold the **SHIFT** key, and **left-click** the
**SuperRoot** object in the **Object tree** window.

After this, you will be able to see the whole 3D scene of the level.
Use the **arrow keys** to move the camera.
**Left-click + move the mouse** to rotate the camera.

Depending on the level, the framerate might be low, as the whole scene is being
rendered.
If you want to render only a part of the level, just find the corresponding object
in the tree and **SHIFT + left-click** it. This way, only this object and all its
subordinates/children will be rendered.

Now, if you want to modify an object (like its position), we first have to select
it. There are two ways to select an object:
 * **Click** its entry in the **Object tree** window.
 * **Right-click** or **Ctrl+Left-click** its 3D model in the 3D view.

Once an object is selected, you can see information on it in the **Object information**
window, like the name, position, orientation, ... It's also possible to edit them
from there.

At the moment, it is recommended to only change the position, orientation and color.
Changing the other values can crash the game.

If you want to save the scene, click the **Save Scene** button in the **c47edit** window.
If the saving fails, be sure that the directory you're saving the file is not read-only
or requires administrator rights.

Then replace the original scene ZIP file with your own and play it! (Make a
backup of the original one first!)

## Interface

### Object tree window
This is where all objects are listed.

**Left-click** to select the object.

**Shift + Left-click** to set object as **view object**/**rendered object**. The object entry will also get the green color to indicate it is the view object.

### Object information window
This window shows information on the currently selected object. You can also do changes on the selected object.

The **Duplicate** button will make a copy of the object. The copies are put at the end of either the **ZROOM::Root** or **ZROOM::ClipRoot** group.
Certain objects, but not all, can make the game crash if they are duplicated, so be careful when using this button.

The **Delete** button will remove the object. I only recommend to delete copies of objects made using the **Duplicate** button. Deleting already existing objects can crash the game.

Clicking **Finding in tree** will make the selected object appear in the object tree (all its parents will be expanded and the selected object will be shown at the middle). This can be useful if you selected an object from its mesh, but want to know where in the object tree it is.

Clicking **Select parent** will select the selected object's parent.

Below the buttons are the object's values:
* **Name** contains the name of the object.
* **State** has an unknown use. Don't change it.
* **Type** contains the type number (will have to find a way to make the text representation appear, but this value is not really useful at the moment). Don't change it.
* **Flags** is also related with the type. Don't change it.
* **PDBL offset** and **PEXC offset** contain some offsets in the PDBL and PEXC chunks respectively. Don't change them.
* **Position** contains the X, Y and Z coordinates of the object's position.
* **Orientation** contains the rotation angles in degrees around the X, Y, Z axes. Note that the order of axis rotations is the following: first Z, then X, then Y.

Depending on the type of the object, additional information might also show up:
 * If the object has a 3D mesh/model, you can change its color, and you can see the count of vertices and polygons.
 * If the object is a light, some parameters are shown (don't know what they mean yet).

### c47edit (Main window)

Here are some commands and settings of the editor.

Click **Save Scene** to save the modified scene.

Click **About...** for some information about the editor.

With **Cam speed** you can change the speed of the camera.

**Cam pos** contains the position of the camera.

**Cam ori** contains the orientation of the camera (angles in radians).

**Cursor pos** contains the position of the white cursor. It is changed when you select an object.

The **Wireframe** checkbox allows you to enable or disable wireframe mode. You can also toggle it by pressing the **W** key.

And lastly, there is a FPS (frames per second) counter.

## Tips

* You can also move the selected object by holding the **Alt** key and **left-clicking** another object. This will put the selected object on the surface you clicked.

* If you want to move characters, be sure to move the group containing the mesh, and not the **Ground** mesh object.

## Contact

Any questions? You can ask me questions per [email](agpetit69@gmail.com).

If you have a problem or found a bug, you can also report it on the [Issues page on GitHub](https://github.com/AdrienTD/c47edit/issues).

Also check the [c47edit's GitHub page](https://github.com/AdrienTD/c47edit) for source code and updates!

Have fun!
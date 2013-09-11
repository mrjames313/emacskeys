EmacsKeys
=========

EmacsKeys is a plugin that brings Emacs key bindings and functionality to the
Qt Creator IDE.

Features
========

EmacsKeys provides the following features:

* EmacsKeys.kms - A Keyboard Mapping Scheme for Qt Creator that can be
imported in Options -> Environment -> Keyboard. It overrides some of the
standard key bindings used in Qt Creator and replaces them with Emacs
ones, or at least things that don't conflict with Emacs ones:
 - Cut (from C-x to C-x C-k)
 - All of the split commands from C-e (0, 1, 2, 3, O) to C-x (0, 1, 2, 3, O)
 - Close (from C-w to C-x C-w)
 - Return to Editor (From Esc to Ctrl-G) /* Doesn't always work */
 - New file (from C-n to C-x, C-n)
 - Got rid of Select All
 - GotoNextWord (from C-T to M-f)
 - GotoPreviousWord (From C-i to M-b)
 - Build (from C-b to C-S-b)
 - got rid of BuildSession
 - Projects mode to C-5
 - Help mode to C-7
 - Undo to C-z
 - Got rid of "SelectBlock up/down" 

* Kill ring - not working, due to tighter integration with qtcreator 
  cut / copy / paste functionality.  Allows for mouse selection and 
  paste as well as use of system clipboard.

* The following keys work as expected: C-n, C-p, C-a, C-e, C-b, C-f, M-b, M-f,
  M-d, M-Backspace, C-d, M-<, M->, C-v, M-v, C-Space, C-k, C-y, C-w, M-w,
  C-l, C-@ M-Space

* C-x,b opens the quick open dialog at the bottom left.

* M-/ triggers the code completion that is triggered by C-Space normally.

* Mnemonics are removed from some of the menus to allow conflicting Emacs keys
  to work.

Build Instructions
==================
Probably could checkout emacskeys before first build, but whatever
* Download the source of Qt Creator 2.5.2 and checkout branch v2.5.2 of this repository
* Do an out-of-source build in (BUILD_DIR)
* cd src/plugins/
* git clone git://github.com/mrjames313/emacskeys.git
* cd (BUILD_DIR)/src/plugins
* make

Install Instructions
====================
It would be nice to get a smoother install process, but this works
to get the system installed qtcreator to load the plugin (on 2.5.2).

Note: Using (INSTALL_LOCATION)=/opt/qtcreator-2.5.2 as global install location,
substitute as necessary

From Qt Creator Source directory:
* sudo cp /src/plugins/emacskeys/EmacsKeys.kms (INSTALL_LOCATION)/share/qtcreator/schemes/
* cd (BUILD_DIR)/lib/qtcreator/plugins/Nokia/
* sudo cp EmacsKeys.pluginspec (INSTALL_LOCATION)/lib/qtcreator/plugins/Nokia
* sudo cp libEmacsKeys.so (INSTALL_LOCATION)/lib/qtcreator/plugins/Nokia
* Fire up qtcreator, goto Tools / Options / Environment / Keyboard and import EmacsKeys.kms 

Credit
======

The Emacs Key code wass based on FakeVim, (C) 2008-2009 Nokia Corporation.
Versions up to 2.2.1 due to Felix Berger
Update to 2.5.2 due to Milan Pipersky
Code cleanup / new feature development due to Michael R. James

License
=======

The code is licensed under the LGPL version 2.1.


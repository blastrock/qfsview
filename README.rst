qFSView
=======

qFSView is a tool for showing disc utilization in a graphical form, much
like the UNIX command 'du'. The visualisation type choosen is a treemap.
Treemaps allow for showing metrics of objects in nested structures, like
sizes of files and directories on your hard disc, where the the size of
directories is defined to be the sum of the size of its children.
Each object is represented by a rectangle which area is proportional to
its metric. The metric must have the property that the sum of the
children's metric of some object is equal or smaller than the objects
metric. This holds true for the file/directory sizes in the use case of
qFSView.

This is a fork of FSView which is a Konqueror plugin that depends on KDE.

.. image:: https://raw.githubusercontent.com/blastrock/qfsview/screenshots/screenshot.png

How to build and run
--------------------

.. code:: bash

  $ mkdir build
  $ cd build
  $ cmake .. -DCMAKE_BUILD_TYPE=Release
  $ make
  $ ./fsview

Additional features over FSView
-------------------------------

- Remove KDE dependency
- Show actual size on disk, better handling sparse files
- Skip mountpoints

Contributors
------------

- Josef Weidendorfer <Josef.Weidendorfer@gmx.de> -- Original author
- Philippe Daouadi <p.daouadi@free.fr> -- Remove KDE dependency

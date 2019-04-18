<p align="center"><img src="doc/testosteron.gif"/><br>
  https://github.com/coderofsalvation/testosteron
</p>

testosteron
===========

deadsimple unit/box/regression-testing-tool using bash & shebang files with *any* programminglanguage.

<p align="center"><img src="doc/testosteron.png"/></p>

Installation 
============

using git
  
    $ git clone https://github.com/coderofsalvation/testosteron 
    $ cd testosteron 
    $ ./testosteron rundir tests/white

or using npm:

    $ npm install testosteron
    $ node_modules/.bin/testosteron rundir node_modules/testosteron/tests/white

Why
===
PHPUnit or whatever-cool-programminglanguage-Unit is great..but sometimes limiting.
For those who need to test on many levels quickly: testosteron is here, which bootstraps any testscript- or testframework.
Most of the time developers know multiple programming languages, so testosteron allows testing in *any* programming language.
Testscripts are very easy, they just pass or fail using exitcode 0 or more.

How
===
Supersimple, just shebang files and symbolic links.
Lets look at the tests:

<img alt="" src="doc/tests.png"/>

Now, since we want to be flexible, we can define presets using symlinks

<img alt="" src="doc/presets.png"/>

After that, just run 1 test:

    ./testosteron run tests/white/10001-test.js 

Or for a whole dir with tests:

    ./testosteron rundir tests/white 

Or a certain preset:

    ./testosteron rundir presets/offline

Or if you want your tests to fail after a certain executiontime:

    PREFIX="timeout 1.3s" ./testosteron rundir presets/stress

Installation
============
Just drop this repository in your webapplication rootdir, and you are ready to go write some testscripts:

    git clone https://github.com/coderofsalvation/testosteron.git
    cd testosteron && rm -rf doc

What are these colornames?
==========================
Gray are [graybox test](http://en.wikipedia.org/wiki/Gray_box_testing), white are more detailed [whitebox tests](http://en.wikipedia.org/wiki/White-box_testing)
Optionally, make sure you have some cli-entrypoints in your webapplication, so you can easily access website/app configvariables within bash.

Notable features
================
Just using a teaspoon of bash you can:

* measure cpu,memory and i/o usage per test 
* compare with expected output (if a test like '10-foo.js' is accompanied with '10-foo.js.out')
* be flexible when what to tests (presets)
* its superportable: no frameworks/libs, just 1 file with 100% bash

Advanced Usage
==============
Testosteron was made with autodeployment in mind, therefore it works great with GIThooks and/or [nodejs-deploy-githook.bash](https://github.com/coderofsalvation/nodejs-deploy-githook.bash)
 or [project-deploy-githook.bash](https://github.com/coderofsalvation/project-deploy-githook.bash)
If one pushes a new website to the server, testosteron runs all the tests, if something fails, deployment will halt.
Example .ndg/hooks/test :

    ./testosteron rundir presets/deployment || {
      echo "removing last commit since you did not pass testosteron..sorry"
      git reset --hard HEAD~1
    }


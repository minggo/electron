## 如何编译改后的Electron

以`v0.34.2-asar`分支为例，假设你已经clone过Electron代码：

1. 同步Electron代码

```
git fetch git@github.com:fireball-x/electron.git v0.34.2-asar
git checkout -b v0.34.2-asar FETCH_HEAD
```
2. 编译

```
cd ELECTRON-ROOT
./script/bootstrap.py  # 下载libchromimcontent，这个只需要运行一次
./script/update.py
./script/build.py  # 如果要debug版本的话，用 ./scrip/build.py -c D，速度较快
```

3. 生成的Electron在`out/D`或者`out/R`目录下
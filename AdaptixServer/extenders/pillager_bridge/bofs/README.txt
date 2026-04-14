此目录下的 .o 由本扩展 Makefile 在编译阶段生成（src_bof/ 内 make），并复制到 dist/bofs/，
供 teamserver「Pillager Bridge」从服务端直接下发。

勿在 beacon_agent/bofs 下编译；源码位于：

  src_bof/pillager_browser/
  src_bof/pillager_mini/

在本扩展目录执行：
  make
或仓库根：
  make extenders

也可将其它符合 Beacon COFF 的 .o 手工放入本目录（文件名仅字母数字 ._-），再 make。

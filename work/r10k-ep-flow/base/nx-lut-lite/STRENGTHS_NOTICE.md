# 强度缓存来源与许可

本包的 22 个 source.bin 是本次对话已发布 LUT 的原样校验副本；100% 读取用户 looks/ 根目录原文件。25/50/75% 是按 RGB 编码域、输出裁切前混合生成的派生表，不是胶片厂商官方标定，不包含颗粒、光晕或空间处理。

## 来源

- 原三张与额外十二张：WApaperplane/metol 固定提交 cb82f4886c1174261cbba8127000c7b696944ff4 的对应 .bin，按原 AGPL-3.0 归属保留。此前数据包的 UPSTREAM/归属资料在 look-sources/metol/。
- kodak_5207_apx、kodak_5219_apx、gold200_warm100_apx、gold200_warm30_apx、两张 portrait 合成：此前 NX-LUT-Portrait-5219-Pack-v1 的近似配方，采用 Spectral Film LUT 0.18.13 与 metol 编码方法。这些不是 EJAY 5207s/Gold200 原表或柯达官方 LUT。
- ejay_skin_native：此前由松下公开实时 LUT 库提供的 EJAY 肤色优化原 VLT 转换所得。EJAY 的原表和派生数据保留 EJAY 归属，不声明原表为本项目 AGPL 作品。来源与重建源码在 look-sources/portrait/，原始 VLT 在其中 source/ejay/，原站 https://www.panasonicshuma.cn/RealtimeLUTLibrary/list.aspx?lcid=32 。

代码许可与数据归属分别保留。完整 AGPL 与 Noto 字形 OFL 在 nx-lut-lite/LICENSE、vendor/；EJAY 数据不混同字体/代码许可。

## 重建

缓存每目录 source.bin 给出精确输入；父目录 manifest.json 记录每张原表 SHA256 和三档输出 SHA256/FNV。将需要重建的原始 .bin 放在普通目录，执行：

    python3 nx-lut-lite/tools/make_strengths.py --looks /path/to/bin_looks --out /path/to/looks/nxl-strengths

该工具只需电脑上的 Python 标准库，不在相机生成或混色。读取每个 CrCbY 节点，转为不裁切 RGB，与该节点原色 RGB 按强度混合，转回 CrCbY 后四舍五入，并重新生成节点填充和尾部。BT.601 为仿射变换，因此与节点三线性插值可交换，误差限于最终量化及矩阵常数近似。

0% 仅用于开发端点验证，不作为相机菜单选项；100% 原样返回输入，不重采样。相机内的强度百分比不是感知强度或线性光亮度。数据没有针对 SRW 显影器做颜色校准。

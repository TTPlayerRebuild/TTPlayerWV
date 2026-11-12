# ttp_wv

独立的 TTPlayer x86 WavPack Reader，静态链接固定版本 WavPack 5.9.0。

- `.wv`；WavPack 4/5 PCM，无损、hybrid 及 IEEE float32。
- 整数原容器输出（8/16/24/32 bit），有效位与真实声道掩码通过 EXTENSIBLE 保留；不经 float32。
- 浮点原始字节输出；不启用 `OPEN_NORMALIZE`，不改变负零、非正规数或非有限位模式。
- 本地同名 `.wvc` 存在时使用校正流；没有校正流的 hybrid 文件仍是有损，不能宣称位精确。
- 64-bit IStream 回调、样本级 Seek、APEv2/ID3v1 标签、APE 封面、终止式 Commit、可选 Probe/SourceInfo。
- 暂不支持 DSD、4.0 之前的 WavPack；不可定位流明确拒绝。未知多声道布局不猜测掩码。

## 构建

```powershell
./build.ps1 -Package -SourcePackage
```

需要 MSVC、CMake、Python 及 Win32 工具链。VC-LTL 5.3.1 / YY-Thunks 1.2.2 固定版本；默认下载依赖并验证 SHA-256。
离线可通过 `-CMakeArguments` 指定 `FETCHCONTENT_SOURCE_DIR_TTPLAYER_WAVPACK`、`FETCHCONTENT_SOURCE_DIR_TTPLAYER_VC_LTL` 和 `FETCHCONTENT_SOURCE_DIR_TTPLAYER_YY_THUNKS`。

运行 ZIP 仅含 `AddIn/ttp_wv.dll` 与 SHA256；source 附件包含插件源码、SDK、依赖源码和许可证。
日期版本在编译前确定；可指定 `-PackageVersion yyyy.MM.dd[pN]`。Action 仅构建/审计/打包，可手动选择发布；不会运行工作区私有测试。

插件代码 MIT；第三方许可分别见 `third_party/wavpack/COPYING`、`docs/licenses/`。
测试放在工作区 `rebuild/tests/audio_format_extensions_20261111`，结果不等同于原版 GUI 或 XP/Win7 实机验证。

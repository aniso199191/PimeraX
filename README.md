# PimeraX

自研 AI 推理框架，不依赖任何现有 AI 框架。C++17 零依赖，mmap 按需加载。

Self-authored AI inference framework. Zero dependencies on PyTorch/TensorFlow/llama.cpp. C++17, mmap on-demand loading.

---

## 量化标准（新命名）

| 名 | 对标 MLX | 位宽 | 体积比 |
|---|---|---|---|
| Bten1 | 4bit | 4 | 0.125× |
| Asw | 6bit | 6 | 0.19× |
| ALX | 8bit | 8 | 0.25× |

## 子框架

| 名 | 用途 |
|---|---|
| Xingeine | 视频生成 |
| Lolam | 语言模型 |
| Imaging | 图片生成 |
| Xido | 多模态（语言+视频+图片识别+解析） |

## PRX 超压缩

后缀 `.prx`，zstd-22 + xz 双通道。实测 62M 模型 248.9MB → 63.4MB（25.5%）。

## 编译

```bash
g++ -O3 -std=c++17 -o pimerax pimerax.cpp
```

## 用法

```bash
./pimerax info model.prx    # 查看模型信息
./pimerax gen model.prx     # 逐 token 生成
```

## License

MIT

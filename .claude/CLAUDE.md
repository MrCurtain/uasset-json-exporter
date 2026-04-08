# UAsset Json Exporter

Unreal Engine 5 Editor-only plugin. Commandlet 架构，将二进制 UAsset 导出为 JSON。

## 项目结构

```
src/                              UE5 插件（复制到 Plugins/ 即可使用）
├── UAssetJsonExporter.uplugin
└── Source/UAssetJsonExporter/
    ├── Public/                   Headers
    ├── Private/                  Implementation
    └── UAssetJsonExporter.Build.cs
```

## 版本管理

版本号定义在 `src/Source/UAssetJsonExporter/Public/UAssetJsonExporterVersion.h`。
修改版本时同步更新 `.uplugin` 的 `Version` 和 `VersionName` 字段。

## 代码规范

- Allman 风格大括号
- 4 空格缩进
- Log category: `LogUAssetJsonExporter`
- 不依赖任何项目模块，只使用 Engine API 和引擎插件（Niagara）
- Commandlet 命名：`{AssetType}{Content}ExportCommandlet`

## Commandlet 添加流程

1. 在 `Public/` 创建 `{Name}Commandlet.h`，继承 `UCommandlet`
2. 在 `Private/` 创建对应 `.cpp`
3. Include `UAssetJsonExporterModule.h`（提供 log category）和 `UAssetJsonExporterVersion.h`
4. `Main()` 开头打印版本号
5. 输出路径统一使用 `Intermediate/UAssetExport/`
6. JSON 必须包含 `ExporterVersion` 和 `ExportType` 字段
7. 如需新的 Engine module 依赖，添加到 `Build.cs`；如需引擎插件依赖，同步添加到 `.uplugin` 的 `Plugins` 数组
8. 更新 README 的 Exporter 表格

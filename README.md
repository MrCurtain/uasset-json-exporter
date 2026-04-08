# uasset-json-exporter

![Claude Code](https://img.shields.io/badge/Claude_Code-black?style=flat&logo=anthropic&logoColor=white)
![Unreal Engine 5](https://img.shields.io/badge/Unreal_Engine-5.7-blue?logo=unrealengine&logoColor=white)
![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)

将 Unreal Engine 的二进制资产导出为结构化 JSON，供 AI 工具链和外部分析工具消费。

## 它解决什么问题

UE 项目中大量逻辑和配置锁在二进制 `.uasset` 文件里。当你让 AI 协助调试或重构时，通常会遇到：

- **"我无法打开二进制文件"**，AI 对 `.uasset` 束手无策，只能把问题丢回给你
- **蓝图过大，难以 C++ 化**，数百个节点的 EventGraph 没有可读的文本表示，人工逐节点翻译既慢又容易遗漏
- **蓝图中的死代码和错误配置**，创建者留下的废弃变量、断开的连接、错误的默认值，肉眼在编辑器里很难全面排查
- **AnimMontage 的 Notify 时序难以追踪**，ANS 的触发时间、持续时长、参数散落在时间轴上，代码侧看不到全貌
- **Widget 布局和动画只存在于编辑器里**，UMG 的层级结构、Slot 设置、关键帧序列没有文本化的审查手段
- **特效和材质参数分散在编辑器各处**，Niagara 的 Emitter 配置、Material 的节点连接和参数覆盖，没有统一的文本审查方式
- **DataTable 和 DataAsset 的配置数据不可搜索**，几十行的数据表在编辑器里逐行翻看效率极低

这些场景的共性：信息被锁在编辑器 UI 中，无法被代码审查、AI 分析、或自动化管道消费。

## 它如何解决

插件通过 UE Commandlet 在编辑器外运行，遍历资产的内部结构，序列化为 JSON。不修改任何资产，只读取和导出。

导出的 JSON 包含完整的结构信息：节点、连接、属性、默认值、时间轴标记等。AI 工具可以直接 grep 和读取 JSON 来理解蓝图逻辑，定位问题，或辅助重构。

## 支持的 Exporter

| Commandlet | `-run=` 名称 | 导出内容 |
|---|---|---|
| `BlueprintEdGraphExportCommandlet` | `BlueprintEdGraphExport` | Blueprint 的 EdGraph 节点、Pin、连接、变量、组件、引用资产 |
| `AnimMontageExportCommandlet` | `AnimMontageExport` | Montage 的 Section、Slot、ANS/AN 位置与时长、Notify 自定义参数 |
| `WidgetLayoutExportCommandlet` | `WidgetLayoutExport` | Widget 的树形层级、Slot 布局属性、子类属性、动画关键帧、EdGraph |
| `DataAssetExportCommandlet` | `DataAssetExport` | DataAsset 子类的所有自定义属性，含数组元素展开 |
| `DataTableExportCommandlet` | `DataTableExport` | DataTable 的行结构名称、所有行数据（按 RowName 索引） |
| `NiagaraSystemExportCommandlet` | `NiagaraSystemExport` | Niagara System 的 Emitter 列表、Spawn/Update 脚本参数、Renderer 属性 |
| `MaterialExportCommandlet` | `MaterialExport` | Material 节点图（Expression 连接链）、全局设置；MaterialInstance 参数覆盖表 |

## 使用方法

### 命令行

```bash
"<UE_PATH>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "<PROJECT_DIR>/YourProject.uproject" \
    -run=BlueprintEdGraphExport \
    -assets="/Game/Path/BP_A,/Game/Path/BP_B" \
    -nullrhi -nosplash -nosound
```

将 `-run=` 后的名称替换为你需要的 Exporter。所有 Exporter 共享相同的 `-assets=` 参数格式。

如果在 Git Bash 中运行，需要加 `MSYS_NO_PATHCONV=1` 前缀，防止 `/Game/...` 被转换为 Windows 路径。

### 输出

文件输出到 `<ProjectDir>/Intermediate/UAssetExport/<AssetPath>.json`，不会进入版本控制。

每个 JSON 文件都包含 `ExporterVersion` 和 `ExportType` 字段，标识导出器版本和资产类型。

### 输出示例

<details>
<summary>Blueprint EdGraph</summary>

```json
{
    "ExporterVersion": "1.0.0",
    "ExportType": "BlueprintEdGraph",
    "Blueprint": "BP_PlayerController",
    "ParentClass": "BW_PlayerController",
    "Variables": [
        { "Name": "DebugTimerHandle", "Type": "FTimerHandle" }
    ],
    "Graphs": [
        {
            "GraphType": "EventGraph",
            "Nodes": [
                {
                    "Class": "K2Node_CallFunction",
                    "Title": "Open Level (by Object Reference)",
                    "FunctionName": "OpenLevelBySoftObjectPtr",
                    "Pins": [ ... ]
                }
            ]
        }
    ]
}
```
</details>

<details>
<summary>AnimMontage</summary>

```json
{
    "ExporterVersion": "1.0.0",
    "ExportType": "AnimMontage",
    "MontageName": "AM_Player_DashStrike_01",
    "SequenceLength": 0.543,
    "Sections": [
        { "Name": "Default", "StartTime": 0 }
    ],
    "SlotTracks": [
        {
            "SlotName": "DefaultSlot",
            "Segments": [
                { "AnimSequence": "AS_Player_DashStrike_01", "AnimPlayRate": 2 }
            ]
        }
    ],
    "Notifies": [
        {
            "NotifyName": "BW_ANS_Displacement",
            "TriggerTime": 0.0001,
            "Duration": 0.122,
            "IsState": true,
            "NotifyClass": "BW_AnimNotifyState_Displacement",
            "Parameters": {
                "DisplacementSpeed": "2000.000000",
                "Curve": "/Script/Engine.CurveFloat'.../DropOff.DropOff'"
            }
        }
    ]
}
```
</details>

<details>
<summary>Widget Layout</summary>

```json
{
    "ExporterVersion": "1.0.0",
    "ExportType": "WidgetLayout",
    "WidgetBlueprint": "WBP_PauseMenu",
    "WidgetTree": {
        "Name": "CanvasPanel_36",
        "Class": "CanvasPanel",
        "Visibility": "SelfHitTestInvisible",
        "Children": [
            {
                "Name": "Tint",
                "Class": "Image",
                "Properties": {
                    "Brush": "(TintColor=...)"
                },
                "Slot": {
                    "SlotClass": "CanvasPanelSlot",
                    "LayoutData": "(Anchors=(Minimum=(X=0,Y=0),Maximum=(X=1,Y=1)))"
                }
            }
        ]
    },
    "Animations": [ ... ],
    "Graphs": [ ... ]
}
```
</details>

<details>
<summary>DataTable</summary>

```json
{
    "ExporterVersion": "1.0.0",
    "ExportType": "DataTable",
    "DataTableName": "DT_PlayerAttributeInit",
    "RowStruct": "AttributeMetaData",
    "RowCount": 5,
    "Rows": {
        "BW_CommonAttributeSet.HealthPoint": {
            "BaseValue": "100.000000",
            "MinValue": "0.000000",
            "MaxValue": "1.000000",
            "bCanStack": "False"
        },
        "BW_PlayerAttributeSet.BloodPoint": {
            "BaseValue": "0.000000",
            "MinValue": "0.000000",
            "MaxValue": "1.000000"
        }
    }
}
```
</details>

<details>
<summary>Material</summary>

```json
{
    "ExporterVersion": "1.0.0",
    "ExportType": "Material",
    "MaterialName": "M_BatParticles",
    "ShadingModel": "MSM_DefaultLit",
    "BlendMode": "BLEND_Translucent",
    "TwoSided": false,
    "MaterialDomain": "MD_Surface",
    "Expressions": [
        {
            "Class": "MaterialExpressionMaterialFunctionCall",
            "Description": "MaterialFunctionCall (FlipBook)",
            "Inputs": [
                {
                    "InputName": "Animation Phase (0-1) (S)",
                    "ConnectedTo": "MaterialExpressionFrac_0"
                }
            ]
        }
    ],
    "OutputConnections": {
        "EmissiveColor": { "Expression": "MaterialExpressionMultiply_0" },
        "Opacity": { "Expression": "MaterialExpressionMultiply_1" }
    }
}
```

MaterialInstance 导出参数覆盖表：

```json
{
    "ExportType": "MaterialInstance",
    "Parent": "M_BaseMaterial",
    "ScalarParameters": [
        { "Name": "Roughness", "Value": 0.8 }
    ],
    "VectorParameters": [
        { "Name": "BaseColor", "Value": "(R=0.5,G=0.1,B=0.1,A=1.0)" }
    ],
    "TextureParameters": [
        { "Name": "Albedo", "Texture": "T_Stone_D", "TexturePath": "/Game/Textures/T_Stone_D" }
    ]
}
```
</details>

<details>
<summary>Niagara System</summary>

```json
{
    "ExporterVersion": "1.0.0",
    "ExportType": "NiagaraSystem",
    "SystemName": "FXS_Alert",
    "ExposedParameters": [],
    "Emitters": [
        {
            "Name": "DirectionalBurst",
            "Enabled": true,
            "SimTarget": "CPU",
            "SpawnScript": {
                "Parameters": [
                    { "Name": "DirectionalBurst.SpawnRate", "Type": "NiagaraFloat" }
                ]
            },
            "Renderers": [
                {
                    "RendererClass": "NiagaraSpriteRendererProperties",
                    "Properties": { ... }
                }
            ]
        }
    ]
}
```
</details>

### 读取策略

导出的 JSON 可能非常大（例如一个复杂的 PlayerCharacter 蓝图可以超过 8000 行）。建议：

1. 用 grep 定位你关心的节点名称、函数名或 Pin 连接
2. 按行号范围读取相关段落，不要一次性加载整个文件

## 集成到你的项目

### 方式 1：作为项目插件

1. 将 `src/` 目录下的内容复制到你项目的 `Plugins/UAssetJsonExporter/`
2. 在 `.uproject` 的 `Plugins` 数组中添加：

```json
{
    "Name": "UAssetJsonExporter",
    "Enabled": true
}
```

3. 重新生成项目文件并编译

### 方式 2：作为引擎插件

将 `src/` 目录下的内容复制到 `<UE_PATH>/Engine/Plugins/Editor/UAssetJsonExporter/`，所有项目共享。

### 前置条件

- Unreal Engine 5.4+
- 运行 Commandlet 时编辑器必须关闭（`UnrealEditor-Cmd` 会锁定项目）
- 项目必须已编译且包含该插件

## 配合 Claude Code 使用

如果你使用 [Claude Code](https://claude.ai/code) 作为 AI 协作工具，可以在项目的 `.claude/CLAUDE.md` 中添加以下段落，让 AI 知道这个工具的存在和用法：

```markdown
## Tooling: UAsset Json Exporter

Plugin: `Plugins/UAssetJsonExporter` (Editor-only)

### Available Commandlets

| Commandlet | Run Name | Description |
|---|---|---|
| BlueprintEdGraphExportCommandlet | `BlueprintEdGraphExport` | Blueprint graphs, nodes, pins, connections |
| AnimMontageExportCommandlet | `AnimMontageExport` | Montage sections, slots, ANS/AN placement and parameters |
| WidgetLayoutExportCommandlet | `WidgetLayoutExport` | Widget tree, layout, animations, EdGraph |
| DataAssetExportCommandlet | `DataAssetExport` | DataAsset subclass properties |
| DataTableExportCommandlet | `DataTableExport` | DataTable row struct and all row data |
| NiagaraSystemExportCommandlet | `NiagaraSystemExport` | Niagara emitters, scripts, renderers |
| MaterialExportCommandlet | `MaterialExport` | Material expressions and connections; MI parameter overrides |

### Usage

UnrealEditor-Cmd.exe Project.uproject -run=<RunName> -assets="/Game/Path/Asset" -nullrhi -nosplash -nosound

### Output

`Intermediate/UAssetExport/<AssetPath>.json`

### Reading Strategy

Do NOT read the entire file at once. Instead:
1. Use Grep to locate relevant node titles, function names, or pin connections.
2. Use Read with offset/limit to inspect only the relevant sections.

### When to Use

- A task references a Blueprint/Widget and its internal logic is relevant
- A bug may be in Blueprint wiring rather than C++
- Need to verify variable defaults, component setup, or event flow
- Need to check AnimMontage notify timing, DataAsset configuration, or material setup
- Need to inspect Niagara emitter parameters or DataTable values
```

AI 会在相关任务中自动调用 Commandlet 导出并分析资产内容。

## 版本

当前版本：**v1.0.0**

版本号定义在 `src/Source/UAssetJsonExporter/Public/UAssetJsonExporterVersion.h`，同时嵌入在每个导出 JSON 的 `ExporterVersion` 字段中。

## License

[MIT](LICENSE) - Hyrex Chia

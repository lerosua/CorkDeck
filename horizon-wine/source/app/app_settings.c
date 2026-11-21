/*
 * The settings model (app_settings.h): the table of settings and the
 * resolution of the value each one runs with.
 */
#include "app_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../config_json.h"
#include "../launcher_settings.h"

#define UI_TEXT(en, ru, zh) { en, ru, zh }
#define OFF_ON { { "0", "off", UI_TEXT( "Off", "Выкл", "关闭" ) }, { "1", "on", UI_TEXT( "On", "Вкл", "开启" ) } }

static const char *const group_names[APP_GROUP_COUNT][APP_LANG_COUNT] =
{
    [APP_GROUP_GRAPHICS]     = UI_TEXT( "Graphics", "Графика", "图形" ),
    [APP_GROUP_TRANSLATION]  = UI_TEXT( "Translation and processor", "Трансляция и процессор", "指令翻译与处理器" ),
    [APP_GROUP_INPUT]        = UI_TEXT( "Controls", "Управление", "控制" ),
    [APP_GROUP_DIAGNOSTICS]  = UI_TEXT( "Diagnostics", "Диагностика", "诊断" ),
    [APP_GROUP_EXPERIMENTAL] = UI_TEXT( "Experimental", "Экспериментальное", "实验功能" ),
    [APP_GROUP_SHELL]        = UI_TEXT( "Interface", "Интерфейс", "界面" ),
};

static const char *const layer_names[APP_LAYER_COUNT][APP_LANG_COUNT] =
{
    [APP_LAYER_DEFAULT]    = UI_TEXT( "Default", "По умолчанию", "默认" ),
    [APP_LAYER_GLOBAL]     = UI_TEXT( "General settings", "Общие настройки", "通用设置" ),
    [APP_LAYER_PROFILE]    = UI_TEXT( "This application's profile", "Профиль приложения", "此应用的配置" ),
    [APP_LAYER_LIMIT]      = UI_TEXT( "Compatibility limit", "Ограничение совместимости", "兼容性限制" ),
    [APP_LAYER_EXPERIMENT] = UI_TEXT( "Test file on the card", "Тестовый файл на карте", "存储卡上的测试文件" ),
};

static const char *const reason_texts[APP_REASON_COUNT][APP_LANG_COUNT] =
{
    [APP_REASON_NONE] = UI_TEXT( "", "", "" ),
    [APP_REASON_VKD3D_NEEDS_OFFICIAL] = UI_TEXT(
        "DXVK + VKD3D needs an Official or GPLAsync DXVK of version 2.1 or newer, so the newest Official one is used.",
        "DXVK + VKD3D требует DXVK Official или GPLAsync версии 2.1 и новее, поэтому взята последняя Official.", "DXVK + VKD3D 需要 2.1 或更高版本的官方 DXVK 或 GPLAsync，已选用最新的官方版本。" ),
    [APP_REASON_NO_VULKAN] = UI_TEXT(
        "This build has no Vulkan, so Wine's own renderer is used.",
        "В этой сборке нет Vulkan, поэтому используется собственный рендер Wine.", "此版本不支持 Vulkan，已使用 Wine 自带的渲染器。" ),
    [APP_REASON_NOT_X86] = UI_TEXT(
        "A native ARM64 program runs without a translator.",
        "Родная программа ARM64 выполняется без транслятора.", "原生 ARM64 程序无需指令翻译器。" ),
    [APP_REASON_LSFG_MISSING] = UI_TEXT(
        "Lossless.dll is not on the card (switch/wine/lsfg), so frame generation is off.",
        "На карте нет Lossless.dll (switch/wine/lsfg), поэтому генерация кадров выключена.", "卡上 switch/wine/lsfg 中没有 Lossless.dll，已关闭帧生成。" ),
    [APP_REASON_FORWARDER_OLD] = UI_TEXT(
        "The installed forwarder does not give the fourth core; reinstall it in System maintenance.",
        "Установленный forwarder не даёт четвёртое ядро; переустановите его в «Обслуживании системы».", "已安装的快捷入口无法使用第四个核心。请在系统维护中重新安装。" ),
    [APP_REASON_FEX_MISSING] = UI_TEXT(
        "The FEX module for this program's architecture is not installed.",
        "Модуль FEX для архитектуры этой программы не установлен.", "尚未安装与该程序架构匹配的 FEX 模块。" ),
    [APP_REASON_DEVELOPER_OFF] = UI_TEXT(
        "Developer mode is off (Settings, Diagnostics), so nothing is recorded beyond failures.",
        "Режим разработчика выключен («Настройки» → «Диагностика»), поэтому записываются только отказы.", "开发者模式已关闭（设置 → 诊断），仅记录故障。" ),
    [APP_REASON_DIAGNOSTICS_OFF] = UI_TEXT(
        "Diagnostics are switched off as a whole.",
        "Диагностика выключена общим выключателем.", "诊断总开关已关闭。" ),
    [APP_REASON_NEEDS_EXTENDED] = UI_TEXT(
        "This affects how the program runs, so it works only in Extended diagnostics.",
        "Это влияет на выполнение программы, поэтому работает только в режиме «Расширенная диагностика».", "此项会影响程序运行，仅在扩展诊断模式下生效。" ),
    [APP_REASON_EXPERIMENT_FILE] = UI_TEXT(
        "A test file in the runtime folder on the card overrides this setting.",
        "Тестовый файл в папке приложения на карте переопределяет эту настройку.", "存储卡运行时目录中的测试文件覆盖了此设置。" ),
};

static const struct app_setting_def defs[APP_SET_COUNT] =
{
    [APP_SET_RENDERER] = { APP_SET_RENDERER, APP_GROUP_GRAPHICS, APP_TYPE_CHOICE, "d3d", "d3d9", NULL, 0, NULL, 1, 3,
        { { "wine", NULL, UI_TEXT( "Wine (OpenGL)", "Wine (OpenGL)", "Wine（OpenGL）" ) },
          { NULL, NULL, UI_TEXT( "DXVK", "DXVK", "DXVK" ) },
          { "dxvk+vkd3d", "dxvk", UI_TEXT( "DXVK + VKD3D", "DXVK + VKD3D", "DXVK + VKD3D" ) } },
        UI_TEXT( "Direct3D renderer", "Рендер Direct3D", "Direct3D 渲染器" ),
        UI_TEXT( "What turns the program's Direct3D calls into the console's graphics. Wine uses its built-in "
               "OpenGL renderer; DXVK uses Vulkan for Direct3D 8 to 11; DXVK + VKD3D adds Direct3D 12. "
               "Graphics DLLs lying beside the program are loaded first whatever is chosen here.",
               "Чем вызовы Direct3D программы превращаются в графику консоли. Wine — встроенный рендер через "
               "OpenGL; DXVK — Vulkan для Direct3D 8–11; DXVK + VKD3D добавляет Direct3D 12. "
               "Графические DLL, лежащие рядом с программой, загружаются первыми при любом выборе.", "选择如何处理 Direct3D 调用。Wine 使用内置的 OpenGL 渲染器；DXVK 通过 Vulkan 支持 Direct3D 8–11；DXVK + VKD3D 还支持 Direct3D 12。程序目录中的图形 DLL 始终优先加载。" ),
        UI_TEXT( "Decides speed and compatibility more than any other setting, and which one is better depends "
               "on the program. DXVK compiles shaders as they are first met, which shows as short stutters.",
               "Сильнее других настроек влияет на скорость и совместимость; что лучше — зависит от программы. "
               "DXVK компилирует шейдеры при первой встрече, это заметно как короткие подтормаживания.", "此项对性能和兼容性的影响最大，最佳选择取决于具体程序。DXVK 首次编译着色器时可能短暂卡顿。" ) },

    [APP_SET_DXVK_SOURCE] = { APP_SET_DXVK_SOURCE, APP_GROUP_GRAPHICS, APP_TYPE_CHOICE, "dxvk-source", NULL, NULL, 0, NULL, 0, 3,
        { { NULL, NULL, UI_TEXT( "Official", "Official", "官方版" ) },
          { "sarek", NULL, UI_TEXT( "Sarek", "Sarek", "Sarek" ) },
          { "gplasync", NULL, UI_TEXT( "GPLAsync", "GPLAsync", "GPLAsync" ) } },
        UI_TEXT( "DXVK variant", "Вариант DXVK", "DXVK 分支" ),
        UI_TEXT( "Which build of DXVK is used. Official is the upstream release. Sarek is a branch for older "
               "graphics hardware and APIs. GPLAsync compiles pipelines in the background.",
               "Какая сборка DXVK используется. Official — оригинальный выпуск. Sarek — ветка для старого "
               "графического оборудования и API. GPLAsync компилирует конвейеры в фоне.", "选择 DXVK 的实现。官方版由上游发布；Sarek 面向旧显卡和图形 API；GPLAsync 会在后台编译管线。" ),
        UI_TEXT( "GPLAsync can reduce stutter at the price of objects appearing late; Sarek cannot be combined "
               "with VKD3D. The effect differs from program to program.",
               "GPLAsync может уменьшить подтормаживания ценой позднего появления объектов; Sarek несовместим "
               "с VKD3D. Эффект различается от программы к программе.", "GPLAsync 可减少卡顿，但部分物体可能延迟显示。Sarek 无法与 VKD3D 组合使用。实际效果因程序而异。" ) },

    [APP_SET_DXVK_VERSION] = { APP_SET_DXVK_VERSION, APP_GROUP_GRAPHICS, APP_TYPE_TEXT, "dxvk-version", NULL, NULL, 0, NULL, 0, 0, {{0}},
        UI_TEXT( "DXVK version", "Версия DXVK", "DXVK 版本" ),
        UI_TEXT( "A particular DXVK release. Left empty, the newest installed one is used.",
               "Конкретный выпуск DXVK. Если не задано — используется самый новый из установленных.", "指定 DXVK 版本。留空时使用已安装的最新版本。" ),
        UI_TEXT( "An older release sometimes runs a program a newer one does not, and the other way round.",
               "Старый выпуск иногда запускает программу, с которой не справляется новый, и наоборот.", "某些程序只能在特定版本中正常运行。" ) },

    [APP_SET_VKD3D_VERSION] = { APP_SET_VKD3D_VERSION, APP_GROUP_GRAPHICS, APP_TYPE_TEXT, "vkd3d-version", NULL, NULL, 0, NULL, 0, 0, {{0}},
        UI_TEXT( "VKD3D version", "Версия VKD3D", "VKD3D 版本" ),
        UI_TEXT( "A particular VKD3D-Proton release for Direct3D 12. Left empty, the newest installed one is used.",
               "Конкретный выпуск VKD3D-Proton для Direct3D 12. Если не задано — самый новый из установленных.", "指定用于 Direct3D 12 的 VKD3D-Proton 版本。留空时使用已安装的最新版本。" ),
        { NULL, NULL } },

    [APP_SET_DXVK_HUD] = { APP_SET_DXVK_HUD, APP_GROUP_GRAPHICS, APP_TYPE_CHOICE, "dxvk-hud", NULL, "dxvk-hud", 0, NULL, 0, 4,
        { { "0", "off", UI_TEXT( "Off", "Выкл", "关闭" ) },
          { "fps", NULL, UI_TEXT( "Frame rate", "Частота кадров", "帧率" ) },
          { "api,fps,frametimes", NULL, UI_TEXT( "Compact", "Краткий", "精简" ) },
          { "version,api,devinfo,fps,memory,frametimes,compiler", NULL, UI_TEXT( "Full", "Полный", "完整" ) } },
        UI_TEXT( "Show FPS and frame times", "Показ FPS и времени кадра", "显示帧率与帧时间" ),
        UI_TEXT( "DXVK's own figures drawn over the program: frames per second, frame times, the Direct3D "
               "version, video memory and shader compilation. It does not cover VKD3D's Direct3D 12.",
               "Собственные показатели DXVK поверх программы: кадры в секунду, время кадра, версия Direct3D, "
               "видеопамять и компиляция шейдеров. Не охватывает Direct3D 12 через VKD3D.", "在画面上显示 DXVK 的帧率、帧时间、Direct3D 版本、显存及着色器编译信息。不涵盖 VKD3D 的 Direct3D 12。" ),
        UI_TEXT( "Costs a little drawing time, and switches off DXVK's descriptor buffers while it is shown.",
               "Немного расходует время отрисовки и отключает descriptor buffers в DXVK, пока показан.", "会占用少量绘制时间；显示期间将关闭 DXVK 的描述符缓冲区。" ) },

    [APP_SET_FRAME_LIMIT] = { APP_SET_FRAME_LIMIT, APP_GROUP_GRAPHICS, APP_TYPE_CHOICE, "frame-limit", NULL, NULL, 0, NULL, 0, 8,
        { { NULL, "Off", UI_TEXT( "Off", "Выкл", "关闭" ) }, { "30", NULL, UI_TEXT( "30", "30", "30" ) },
          { "40", NULL, UI_TEXT( "40", "40", "40" ) }, { "45", NULL, UI_TEXT( "45", "45", "45" ) },
          { "60", NULL, UI_TEXT( "60", "60", "60" ) }, { "75", NULL, UI_TEXT( "75", "75", "75" ) },
          { "90", NULL, UI_TEXT( "90", "90", "90" ) }, { "120", NULL, UI_TEXT( "120", "120", "120" ) } },
        UI_TEXT( "Frame rate limit", "Ограничение частоты кадров", "帧率上限" ),
        UI_TEXT( "The most frames per second the program may draw through Vulkan, DXVK and VKD3D. VSync and "
               "the program's own limit still apply.",
               "Наибольшее число кадров в секунду, которое программа может рисовать через Vulkan, DXVK и "
               "VKD3D. VSync и собственное ограничение программы продолжают действовать.", "限制通过 Vulkan、DXVK 和 VKD3D 绘制的每秒帧数。垂直同步和程序自身的帧率限制仍然有效。" ),
        UI_TEXT( "A limit below what the console manages gives an even frame time and less heat.",
               "Ограничение ниже возможностей консоли даёт ровное время кадра и меньший нагрев.", "将上限设在主机能稳定达到的帧率以下，有助于稳定帧时间并减少发热。" ) },

    [APP_SET_VSYNC] = { APP_SET_VSYNC, APP_GROUP_GRAPHICS, APP_TYPE_SWITCH, "vsync", NULL, NULL, 0, NULL, 1, 2, OFF_ON,
        UI_TEXT( "VSync", "Вертикальная синхронизация", "垂直同步" ),
        UI_TEXT( "Shows each frame in step with the screen's refresh.",
               "Показывает каждый кадр в такт обновлению экрана.", "使每帧的显示与屏幕刷新同步。" ),
        UI_TEXT( "Off removes the wait for the screen but can tear the picture. Frame generation always "
               "presents in step with the screen.",
               "Выключение убирает ожидание экрана, но может давать разрывы изображения. Генерация кадров "
               "всегда выводит в такт экрану.", "关闭后无需等待刷新，但画面可能撕裂。帧生成始终与屏幕刷新同步。" ) },

    [APP_SET_UPSCALING] = { APP_SET_UPSCALING, APP_GROUP_GRAPHICS, APP_TYPE_CHOICE, "upscaling", "upscale", NULL, 0, NULL, 0, 3,
        { { NULL, "off", UI_TEXT( "Off (bilinear)", "Выкл (билинейное)", "关闭（双线性）" ) },
          { "fsr", "1", UI_TEXT( "FSR 1.0", "FSR 1.0", "FSR 1.0" ) },
          { "integer", "2", UI_TEXT( "Integer", "Целочисленное", "整数倍" ) } },
        UI_TEXT( "Upscaling", "Масштабирование", "画面放大" ),
        UI_TEXT( "How a picture drawn with fewer pixels than the screen is enlarged. FSR 1.0 keeps edges and "
               "sharpens; Integer enlarges by whole steps with square pixels, for pixel art.",
               "Как увеличивается изображение, нарисованное с меньшим числом пикселей, чем у экрана. "
               "FSR 1.0 сохраняет контуры и добавляет резкость; целочисленное увеличивает целыми шагами "
               "с квадратными пикселями — для пиксельной графики.", "将低分辨率画面放大至屏幕大小。FSR 1.0 保留边缘并锐化图像；整数倍缩放保持像素方正，适合像素画风。" ),
        UI_TEXT( "FSR takes some GPU time each frame. Integer leaves wider black bars.",
               "FSR расходует часть времени GPU на каждом кадре. Целочисленное оставляет более широкие чёрные поля.", "FSR 每帧会占用少量 GPU 时间。整数倍缩放可能留下较宽的黑边。" ) },

    [APP_SET_SHARPNESS] = { APP_SET_SHARPNESS, APP_GROUP_GRAPHICS, APP_TYPE_CHOICE, "upscaling-sharpness", "sharpness", NULL, 0, NULL, 2, 6,
        { { "0%", NULL, UI_TEXT( "0%", "0%", "0%" ) }, { "20%", NULL, UI_TEXT( "20%", "20%", "20%" ) },
          { "40%", NULL, UI_TEXT( "40%", "40%", "40%" ) }, { "60%", NULL, UI_TEXT( "60%", "60%", "60%" ) },
          { "80%", NULL, UI_TEXT( "80%", "80%", "80%" ) }, { "100%", NULL, UI_TEXT( "100%", "100%", "100%" ) } },
        UI_TEXT( "FSR sharpness", "Резкость FSR", "FSR 锐度" ),
        UI_TEXT( "How strongly FSR's second pass sharpens the enlarged picture.",
               "Насколько сильно второй проход FSR повышает резкость увеличенного изображения.", "设置 FSR 第二阶段的锐化强度。" ),
        { NULL, NULL } },

    [APP_SET_LSFG] = { APP_SET_LSFG, APP_GROUP_GRAPHICS, APP_TYPE_SWITCH, "lsfg", NULL, NULL, 0, NULL, 0, 2, OFF_ON,
        UI_TEXT( "Frame generation (LSFG-VK, 2x)", "Генерация кадров (LSFG-VK, 2x)", "帧生成（LSFG-VK，2 倍）" ),
        UI_TEXT( "Draws one made-up frame between each two real ones, for Vulkan, DXVK and VKD3D. It needs "
               "your own Lossless.dll in switch/wine/lsfg.",
               "Рисует один вычисленный кадр между каждыми двумя настоящими — для Vulkan, DXVK и VKD3D. "
               "Нужна ваша собственная Lossless.dll в switch/wine/lsfg.", "在两帧之间插入一帧合成画面，支持 Vulkan、DXVK 和 VKD3D。请将自己的 Lossless.dll 放入 switch/wine/lsfg。" ),
        UI_TEXT( "Motion looks smoother, but the program does not respond faster, input lags by about a "
               "frame, and GPU time and memory are spent on it. Not a remedy for a low frame rate.",
               "Движение выглядит плавнее, но отклик программы не ускоряется, ввод запаздывает примерно "
               "на кадр, расходуются время GPU и память. Не лекарство от низкой частоты кадров.", "运动画面会更平滑，但响应速度不会提升，输入延迟约增加一帧，且会占用 GPU 时间和内存。基础帧率过低时帮助有限。" ) },

    [APP_SET_LSFG_PERFORMANCE] = { APP_SET_LSFG_PERFORMANCE, APP_GROUP_GRAPHICS, APP_TYPE_SWITCH, "lsfg-performance", NULL, NULL, 0, NULL, 1, 2, OFF_ON,
        UI_TEXT( "Frame generation: performance mode", "Генерация кадров: режим производительности", "帧生成：性能模式" ),
        UI_TEXT( "Uses the cheaper of LSFG's two paths.", "Использует более дешёвый из двух путей LSFG.", "使用 LSFG 开销较低的处理方式。" ),
        UI_TEXT( "Off gives the quality path, which takes more GPU time.",
               "Выключение включает путь качества, он требует больше времени GPU.", "关闭后使用画质模式，占用更多 GPU 时间。" ) },

    [APP_SET_LSFG_FLOW] = { APP_SET_LSFG_FLOW, APP_GROUP_GRAPHICS, APP_TYPE_CHOICE, "lsfg-flow", NULL, NULL, 0, NULL, 1, 3,
        { { "0.125", NULL, UI_TEXT( "12.5%", "12,5%", "12.5%" ) }, { "0.25", NULL, UI_TEXT( "25%", "25%", "25%" ) },
          { "0.5", NULL, UI_TEXT( "50%", "50%", "50%" ) } },
        UI_TEXT( "Frame generation: motion resolution", "Генерация кадров: разрешение оценки движения", "帧生成：运动估算分辨率" ),
        UI_TEXT( "The size of the picture motion is estimated on.",
               "Размер изображения, по которому оценивается движение.", "用于估算画面运动的图像分辨率。" ),
        UI_TEXT( "Lower values take less GPU time and memory and estimate motion more roughly.",
               "Меньшие значения требуют меньше времени GPU и памяти, но оценивают движение грубее.", "调低可节省 GPU 时间和内存，但运动估算会更粗略。" ) },

    [APP_SET_TRANSLATOR] = { APP_SET_TRANSLATOR, APP_GROUP_TRANSLATION, APP_TYPE_CHOICE, "cpu", NULL, NULL, 0, NULL, 1, 2,
        { { "box64", NULL, UI_TEXT( "Box64", "Box64", "Box64" ) }, { NULL, NULL, UI_TEXT( "FEX", "FEX", "FEX" ) } },
        UI_TEXT( "Processor translator", "Транслятор процессора", "CPU 指令翻译器" ),
        UI_TEXT( "What turns the program's x86 instructions into the console's ARM64 ones. FEX is the main "
               "one; Box64 is the alternative where it is supported.",
               "Что превращает инструкции x86 программы в инструкции ARM64 консоли. FEX — основной; "
               "Box64 — альтернатива там, где он поддерживается.", "将程序的 x86 指令翻译为主机的 ARM64 指令。通常使用 FEX；在支持的场合也可选择 Box64。" ),
        UI_TEXT( "Speed and compatibility differ from program to program; neither is faster everywhere.",
               "Скорость и совместимость различаются от программы к программе; ни один не быстрее везде.", "速度和兼容性取决于程序，没有一种翻译器在所有场景下都更快。" ) },

    [APP_SET_FOUR_CORES] = { APP_SET_FOUR_CORES, APP_GROUP_TRANSLATION, APP_TYPE_SWITCH, "four-cores", NULL, NULL, 0, NULL, 1, 2, OFF_ON,
        UI_TEXT( "Fourth core", "Четвёртое ядро", "第四个核心" ),
        UI_TEXT( "Lets the program's helper threads use core 3, which the console otherwise keeps for the system.",
               "Разрешает вспомогательным потокам программы использовать ядро 3, которое консоль обычно "
               "оставляет системе.", "允许程序的辅助线程使用通常留给系统的核心 3。" ),
        UI_TEXT( "Usually helps programs with several busy threads. The system also runs on that core, so "
               "the gain is not guaranteed.",
               "Обычно помогает программам с несколькими занятыми потоками. На этом ядре работает и система, "
               "поэтому выигрыш не гарантирован.", "通常有利于多个线程都很繁忙的程序。系统也在该核心上运行，实际收益视情况而定。" ) },

    [APP_SET_SYNC] = { APP_SET_SYNC, APP_GROUP_TRANSLATION, APP_TYPE_CHOICE, "sync", NULL, NULL, 0, NULL, 0, 2,
        { { NULL, NULL, UI_TEXT( "Standard", "Стандартная", "标准" ) }, { "horizon", NULL, UI_TEXT( "Horizon", "Horizon", "Horizon" ) } },
        UI_TEXT( "Thread synchronisation", "Синхронизация потоков", "线程同步" ),
        UI_TEXT( "How the program's threads wait for each other. Standard asks the built-in Wine server; "
               "Horizon waits directly on the console's own objects.",
               "Как потоки программы ждут друг друга. Стандартная обращается к встроенному серверу Wine; "
               "Horizon ждёт напрямую на объектах самой консоли.", "设置线程如何相互等待。标准模式通过内置 Wine 服务同步；Horizon 模式直接等待主机的系统对象。" ),
        UI_TEXT( "Horizon can be faster in programs that wait very often, and is less widely tested.",
               "Horizon может быть быстрее в программах с очень частыми ожиданиями, но проверена на меньшем "
               "числе программ.", "对于频繁等待的程序，Horizon 模式可能更快，但测试覆盖较少。" ) },

    [APP_SET_CORE_BALANCING] = { APP_SET_CORE_BALANCING, APP_GROUP_TRANSLATION, APP_TYPE_SWITCH, NULL, NULL, "core-balancing", 0, NULL, 1, 2, OFF_ON,
        UI_TEXT( "Spread threads over cores", "Распределять потоки по ядрам", "跨核心分配线程" ),
        UI_TEXT( "Moves the program's busy threads apart, so two of them do not share one core while another is idle.",
               "Разводит занятые потоки программы, чтобы два из них не делили одно ядро при простаивающем другом.", "分散繁忙线程，避免两个线程挤在同一核心而其他核心空闲。" ),
        UI_TEXT( "Normally on. Off leaves every thread where it started.",
               "Обычно включено. При выключении каждый поток остаётся там, где был запущен.", "默认开启。关闭后各线程保留在其初始核心上。" ) },

    [APP_SET_WORKER_POLICY] = { APP_SET_WORKER_POLICY, APP_GROUP_TRANSLATION, APP_TYPE_CHOICE, "worker-policy", NULL, "cpu-worker-policy", 0, "cpu-worker-policy.txt", 4, 5,
        { { "0", NULL, UI_TEXT( "0 - Graphics workers on core 3", "0 — графические потоки на ядре 3", "0 - 图形工作线程使用核心 3" ) },
          { "1", NULL, UI_TEXT( "1 - Background workers on core 3", "1 — фоновые потоки на ядре 3", "1 - 后台工作线程使用核心 3" ) },
          { "2", NULL, UI_TEXT( "2 - Background workers share cores", "2 — фоновые потоки делят ядра", "2 - 后台工作线程共享核心" ) },
          { "3", NULL, UI_TEXT( "3 - The kernel places every thread", "3 — все потоки размещает ядро системы", "3 - 由内核分配所有线程" ) },
          { "4", NULL, UI_TEXT( "4 - Shared cores, background prefers core 3", "4 — общие ядра, фоновые предпочитают ядро 3", "4 - 共享核心，后台优先使用核心 3" ) } },
        UI_TEXT( "Thread placement policy", "Политика размещения потоков", "线程分配策略" ),
        UI_TEXT( "Which cores the program's own threads and the graphics library's helper threads may run on. "
               "It applies from the next start of the program.",
               "На каких ядрах могут работать потоки самой программы и вспомогательные потоки графической "
               "библиотеки. Применяется со следующего запуска программы.", "指定程序线程和图形库辅助线程可使用的核心。下次启动程序时生效。" ),
        UI_TEXT( "An experimental tuning: the best policy depends on how the program divides its work, and "
               "has to be found by trying.",
               "Экспериментальная подстройка: лучшая политика зависит от того, как программа делит работу, "
               "и подбирается пробой.", "实验性调优。最佳策略取决于程序如何拆分工作，需要实际测试。" ) },

    [APP_SET_SHADER_RAM_MB] = { APP_SET_SHADER_RAM_MB, APP_GROUP_TRANSLATION, APP_TYPE_CHOICE, "shader-ram-cache-mb", NULL, "shader-ram-cache-mb", 0, "shader-ram-cache-mb.txt", 3, 6,
        { { "0", NULL, UI_TEXT( "Off", "Выкл", "关闭" ) }, { "8", NULL, UI_TEXT( "8 MB", "8 МБ", "8 MB" ) },
          { "16", NULL, UI_TEXT( "16 MB", "16 МБ", "16 MB" ) }, { "32", NULL, UI_TEXT( "32 MB", "32 МБ", "32 MB" ) },
          { "48", NULL, UI_TEXT( "48 MB", "48 МБ", "48 MB" ) }, { "64", NULL, UI_TEXT( "64 MB", "64 МБ", "64 MB" ) } },
        UI_TEXT( "Shader cache in memory", "Кэш шейдеров в памяти", "内存着色器缓存" ),
        UI_TEXT( "Keeps compiled shaders in memory as well as on the card, so one met again is not read from the card.",
               "Держит скомпилированные шейдеры в памяти помимо карты, чтобы повторно встреченный не читался с карты.", "除存储卡外，也在内存中保留已编译的着色器，避免重复读卡。" ),
        UI_TEXT( "Takes the chosen amount from the program's memory; it empties itself when memory runs low. "
               "Worth it only where the card is the delay.",
               "Занимает выбранный объём из памяти программы; сам очищается при нехватке памяти. "
               "Оправдан только там, где задержку даёт карта.", "会占用所选大小的程序内存；内存不足时自动清空。仅在读卡造成瓶颈时值得开启。" ) },

    [APP_SET_OWN_CONTROLS] = { APP_SET_OWN_CONTROLS, APP_GROUP_INPUT, APP_TYPE_SWITCH, "own-controls", NULL, NULL, 0, NULL, 0, 2,
        { { "0", "off", UI_TEXT( "Shared", "Общие", "通用" ) }, { "1", "on", UI_TEXT( "Its own", "Свои", "独立" ) } },
        UI_TEXT( "Key mapping", "Назначение клавиш", "按键映射" ),
        UI_TEXT( "Whether this program's own key mapping applies over the shared one. Switching it off keeps "
               "the keys that were set.",
               "Действует ли собственное назначение клавиш этой программы поверх общего. При выключении "
               "заданные клавиши сохраняются.", "使用此程序的独立按键映射，覆盖通用映射。关闭仍会保留已设按键。" ),
        { NULL, NULL } },

    [APP_SET_PAD_AS_KEYBOARD] = { APP_SET_PAD_AS_KEYBOARD, APP_GROUP_INPUT, APP_TYPE_SWITCH, "pad-as-keyboard", NULL, "controller-as-keyboard-and-mouse", 0, NULL, 0, 2, OFF_ON,
        UI_TEXT( "Controller as keyboard and mouse", "Контроллер как клавиатура и мышь", "用手柄模拟键盘和鼠标" ),
        UI_TEXT( "Until the program reads the controller as a gamepad, the sticks and buttons send mouse "
               "movement and keys. Off, the controller is only ever a gamepad (XInput).",
               "Пока программа не читает контроллер как геймпад, стики и кнопки посылают движение мыши "
               "и клавиши. Выключено — контроллер всегда только геймпад (XInput).", "在程序以游戏手柄方式读取输入前，摇杆和按键会模拟鼠标和键盘。关闭后只提供游戏手柄输入（XInput）。" ),
        UI_TEXT( "Needed by programs without gamepad support. In a program that has it, it can make the "
               "pointer move when the stick is used.",
               "Нужно программам без поддержки геймпада. В программе с поддержкой может двигать курсор "
               "при работе стиком.", "不支持手柄的程序需要此功能。对支持手柄的程序，摇杆可能同时移动鼠标指针。" ) },

    [APP_SET_KEYBOARD_ON_FOCUS] = { APP_SET_KEYBOARD_ON_FOCUS, APP_GROUP_INPUT, APP_TYPE_SWITCH, NULL, NULL, "keyboard-on-text-focus", 0, NULL, 1, 2, OFF_ON,
        UI_TEXT( "On-screen keyboard on text fields", "Экранная клавиатура в текстовых полях", "文本框自动弹出键盘" ),
        UI_TEXT( "Opens the on-screen keyboard by itself when a text field takes focus. Minus with a click "
               "of the right stick opens it in any program either way.",
               "Сама открывает экранную клавиатуру, когда фокус получает текстовое поле. «−» вместе с нажатием "
               "правого стика открывает её в любой программе при любом значении.", "文本框获得焦点时自动打开屏幕键盘。无论是否开启，都可按住减号并按下右摇杆手动打开。" ),
        { NULL, NULL } },

    [APP_SET_DEVELOPER] = { APP_SET_DEVELOPER, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, NULL, NULL, "developer-mode", 0, NULL, 0, 2, OFF_ON,
        UI_TEXT( "Developer mode", "Режим разработчика", "开发者模式" ),
        UI_TEXT( "All diagnostics behind one switch: the Diagnostics page of the main menu, detailed logs of "
               "programs, counters, traces, the profiler and the status served over Wi-Fi.",
               "Вся диагностика за одним переключателем: раздел «Диагностика» в главном меню, подробные журналы "
               "программ, счётчики, трассы, профайлер и раздача состояния по Wi-Fi.", "统一控制主菜单诊断页、详细日志、计数器、跟踪、性能分析和通过 Wi-Fi 提供的状态信息。" ),
        UI_TEXT( "Off, only failures are written down. On, a program writes logs to the card and may run slower. "
               "Applies from the next start of a program.",
               "Выключено — записываются только отказы. Включено — программа пишет журналы на карту и может "
               "работать медленнее. Действует со следующего запуска программы.", "关闭时仅记录故障。开启后程序会写入存储卡，可能降低运行速度。下次启动程序时生效。" ) },

    [APP_SET_DIAGNOSTICS_MODE] = { APP_SET_DIAGNOSTICS_MODE, APP_GROUP_DIAGNOSTICS, APP_TYPE_CHOICE, "diagnostics", NULL, "diagnostics", 0, NULL, 2, 3,
        { { "off", NULL, UI_TEXT( "Off", "Выключена", "关闭" ) },
          { "observe", NULL, UI_TEXT( "Observation", "Наблюдение", "观察模式" ) },
          { "extended", NULL, UI_TEXT( "Extended diagnostics", "Расширенная диагностика", "扩展诊断" ) } },
        UI_TEXT( "Diagnostics", "Диагностика", "诊断" ),
        UI_TEXT( "Off records nothing beyond errors. Observation collects figures that do not change how the "
               "program runs. Extended also allows the tools that do: traces, the sampling profiler, wait records.",
               "«Выключена» не записывает ничего, кроме ошибок. «Наблюдение» собирает показатели, не меняющие "
               "выполнение программы. «Расширенная» разрешает и инструменты, которые его меняют: трассы, "
               "сэмплирующий профайлер, записи ожиданий.", "关闭时仅记录错误。观察模式收集不改变程序行为的指标；扩展模式还允许跟踪、采样分析和等待记录。" ),
        UI_TEXT( "Observation is meant to cost nothing noticeable. Extended slows the program down and "
               "writes to the card; do not judge performance in it.",
               "«Наблюдение» рассчитано на незаметную стоимость. «Расширенная» замедляет программу и пишет "
               "на карту; оценивать производительность в ней нельзя.", "观察模式通常不会明显影响性能。扩展模式会降低速度并写卡，不宜用来评估正常性能。" ) },

    [APP_SET_SESSION_LOGS] = { APP_SET_SESSION_LOGS, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, "session-logs", NULL, "session-logs", 0, NULL, 0, 2, OFF_ON,
        UI_TEXT( "Keep this application's logs", "Сохранять логи приложения", "保留此应用的日志" ),
        UI_TEXT( "Keeps the log of each session in a folder of the application's own, one file per session, "
               "instead of overwriting the last one.",
               "Сохраняет лог каждого сеанса в отдельной папке приложения, по файлу на сеанс, вместо "
               "перезаписи последнего.", "在独立目录中为每次会话保存一个日志文件，保留之前的记录。" ),
        UI_TEXT( "Takes space on the card; old sessions are not removed by themselves.",
               "Занимает место на карте; старые сеансы сами не удаляются.", "会占用存储卡空间；旧会话不会自动删除。" ) },

    [APP_SET_PERF_COUNTERS] = { APP_SET_PERF_COUNTERS, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, "performance-counters", NULL, "performance-counters", 0, "performance.txt", 1, 2, OFF_ON,
        UI_TEXT( "Performance counters", "Счётчики производительности", "性能计数器" ),
        UI_TEXT( "Samples a share of the program's timing, wait and graphics calls for the session report.",
               "Выборочно измеряет вызовы времени, ожиданий и графики программы для отчёта о сеансе.", "抽样记录程序计时、等待和图形调用，用于生成会话报告。" ),
        UI_TEXT( "Passive: a sample of calls is timed, the rest are untouched.",
               "Пассивно: измеряется часть вызовов, остальные не затрагиваются.", "仅对部分调用计时，其余调用不受影响。" ) },

    [APP_SET_STALL_SNAPSHOTS] = { APP_SET_STALL_SNAPSHOTS, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, NULL, NULL, "stall-snapshots", 0, "stall-snapshots.txt", 0, 2, OFF_ON,
        UI_TEXT( "Snapshots when the program stalls", "Снимки при зависании программы", "程序卡顿时生成快照" ),
        UI_TEXT( "When no frame has come for a while, writes down what every thread is waiting for.",
               "Когда кадров долго нет, записывает, чего ждёт каждый поток.", "一段时间没有新画面时，记录每个线程正在等待的内容。" ),
        UI_TEXT( "Each snapshot pauses the program's threads for a moment, only while it is already stalled.",
               "Каждый снимок на мгновение приостанавливает потоки программы — только когда она уже стоит.", "只在程序已卡顿时生成快照，每次会短暂暂停各线程。" ) },

    [APP_SET_REMOTE_SNAPSHOT] = { APP_SET_REMOTE_SNAPSHOT, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, NULL, NULL, "remote-snapshot", 0, "remote-snapshot.txt", 1, 2, OFF_ON,
        UI_TEXT( "Status file for a computer", "Файл состояния для компьютера", "供电脑读取的状态文件" ),
        UI_TEXT( "Writes a short status file a computer on the same network can collect while the program runs.",
               "Пишет короткий файл состояния, который компьютер в той же сети может забирать во время работы программы.", "程序运行时写入简短的状态文件，供同一网络中的电脑读取。" ),
        UI_TEXT( "One small write to the card every few seconds.",
               "Одна небольшая запись на карту раз в несколько секунд.", "每隔几秒向存储卡写入一次少量数据。" ) },

    [APP_SET_VERBOSE] = { APP_SET_VERBOSE, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, "verbose", NULL, "verbose-log", 0, NULL, 0, 2, OFF_ON,
        UI_TEXT( "Verbose traces", "Подробные трассы", "详细跟踪" ),
        UI_TEXT( "Writes Wine's warnings and traces to the log.", "Записывает в лог предупреждения и трассы Wine.", "将 Wine 的警告和跟踪信息写入日志。" ),
        UI_TEXT( "Slows the program down noticeably and writes a great deal to the card.",
               "Заметно замедляет программу и много пишет на карту.", "会明显降低程序速度，并向存储卡写入大量数据。" ) },

    [APP_SET_PROFILER] = { APP_SET_PROFILER, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, "profile", NULL, "profiler", 0, NULL, 0, 2, OFF_ON,
        UI_TEXT( "Sampling profiler", "Сэмплирующий профайлер", "采样性能分析器" ),
        UI_TEXT( "Stops every thread at intervals to record where it is, and writes where the time went.",
               "Периодически останавливает каждый поток, чтобы записать, где он находится, и пишет, куда ушло время.", "定期暂停各线程，记录执行位置，统计耗时分布。" ),
        UI_TEXT( "Intrusive: the stops themselves cost time, so frame rates measured with it are not the program's own.",
               "Вмешивается в выполнение: остановки сами отнимают время, поэтому частота кадров с ним — "
               "не собственная частота программы.", "暂停本身需要时间；此时测得的帧率低于正常运行时。" ) },

    [APP_SET_WAIT_DIAGNOSTICS] = { APP_SET_WAIT_DIAGNOSTICS, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, "wait-diagnostics", NULL, "wait-diagnostics", 0, "wait-diagnostics.txt", 1, 2, OFF_ON,
        UI_TEXT( "Wait records", "Записи ожиданий", "等待记录" ),
        UI_TEXT( "Keeps a bounded record of what each thread asked the server and what it waits on, for "
               "finding why a program stands still.",
               "Ведёт ограниченную запись того, что каждый поток запросил у сервера и чего ждёт, — чтобы "
               "найти, почему программа стоит.", "保存有限数量的线程服务请求和等待对象，以排查程序卡住的原因。" ),
        UI_TEXT( "Adds work to every wait.", "Добавляет работу к каждому ожиданию.", "会增加每次等待操作的开销。" ) },

    [APP_SET_WINDOWS] = { APP_SET_WINDOWS, APP_GROUP_EXPERIMENTAL, APP_TYPE_CHOICE, "windows", NULL, "windows-through-opengl", 1, NULL, 0, 2,
        { { "compositor", NULL, UI_TEXT( "Compositor (OpenGL)", "Композитор (OpenGL)", "合成器（OpenGL）" ) },
          { "framebuffer", NULL, UI_TEXT( "Framebuffer", "Кадровый буфер", "帧缓冲区" ) } },
        UI_TEXT( "Windows shown by", "Чем показываются окна", "窗口显示方式" ),
        UI_TEXT( "The compositor draws every window of the program through OpenGL. The framebuffer copies "
               "window pixels straight to the screen, for when the compositor misbehaves.",
               "Композитор рисует каждое окно программы через OpenGL. Кадровый буфер копирует пиксели окон "
               "прямо на экран — на случай, если композитор работает неправильно.", "合成器通过 OpenGL 绘制各个窗口。合成器工作异常时，可改用帧缓冲区将窗口像素直接复制到屏幕。" ),
        UI_TEXT( "The framebuffer is slower and is a fallback, not a tuning.",
               "Кадровый буфер медленнее; это запасной путь, а не подстройка.", "帧缓冲区速度较慢，适用于故障排查。" ) },

    [APP_SET_GL_PINNED_CACHED] = { APP_SET_GL_PINNED_CACHED, APP_GROUP_EXPERIMENTAL, APP_TYPE_SWITCH, NULL, NULL, "gl-pinned-buffers-cached", 0, NULL, 1, 2, OFF_ON,
        UI_TEXT( "OpenGL: cacheable pinned buffers", "OpenGL: кэшируемые закреплённые буферы", "OpenGL：允许缓存固定缓冲区" ),
        UI_TEXT( "Maps the OpenGL driver's pinned GPU buffers so the processor may cache them.",
               "Отображает закреплённые буферы GPU драйвера OpenGL так, чтобы процессор мог их кэшировать.", "将 OpenGL 驱动固定的 GPU 缓冲区映射为处理器可缓存的内存。" ),
        UI_TEXT( "Off restores the older, slower mapping; it exists to compare the two.",
               "Выключение возвращает прежнее, более медленное отображение; существует для сравнения.", "关闭后恢复较慢的旧映射方式，供对比测试。" ) },

    [APP_SET_GL_CLEAN_BEFORE_SUBMIT] = { APP_SET_GL_CLEAN_BEFORE_SUBMIT, APP_GROUP_EXPERIMENTAL, APP_TYPE_SWITCH, NULL, NULL, "gl-clean-before-submit", 0, NULL, 1, 2, OFF_ON,
        UI_TEXT( "OpenGL: flush caches before each submission", "OpenGL: сброс кэшей перед каждой отправкой", "OpenGL：每次提交前刷新缓存" ),
        UI_TEXT( "Makes what the processor wrote into GPU buffers visible to the GPU before commands are sent.",
               "Делает записанное процессором в буферы GPU видимым для GPU перед отправкой команд.", "在发送命令前，确保 GPU 能读取处理器写入缓冲区的数据。" ),
        UI_TEXT( "Off is faster and can corrupt the picture.", "Выключение быстрее, но может портить изображение.", "关闭可能提速，但画面可能出现错误。" ) },

    [APP_SET_DISPLAY_DEVICES] = { APP_SET_DISPLAY_DEVICES, APP_GROUP_EXPERIMENTAL, APP_TYPE_SWITCH, NULL, NULL, "display-devices", 0, NULL, 1, 2, OFF_ON,
        UI_TEXT( "Register display devices", "Регистрировать устройства отображения", "注册显示设备" ),
        UI_TEXT( "Tells Windows programs about the screen as a display adapter and monitor.",
               "Сообщает Windows-программам об экране как о видеоадаптере и мониторе.", "向 Windows 程序报告显示适配器和显示器。" ),
        UI_TEXT( "Some programs do not start without it; off is for comparison.",
               "Некоторые программы без этого не запускаются; выключение — для сравнения.", "部分程序需要此项才能启动。关闭可用于对比测试。" ) },

    [APP_SET_REOPEN_SHELL] = { APP_SET_REOPEN_SHELL, APP_GROUP_SHELL, APP_TYPE_SWITCH, NULL, NULL, "reopen-the-launcher-on-exit", 0, NULL, 1, 2, OFF_ON,
        UI_TEXT( "Come back here when a program ends", "Возвращаться сюда после завершения программы", "程序结束后返回此界面" ),
        UI_TEXT( "Starts the application again when a program closes, instead of going to the HOME menu.",
               "Заново открывает приложение после закрытия программы вместо выхода в меню HOME.", "关闭程序后重新打开 CorkDeck，返回应用界面。" ),
        { NULL, NULL } },

    [APP_SET_DXVK_FOR_NEW] = { APP_SET_DXVK_FOR_NEW, APP_GROUP_SHELL, APP_TYPE_SWITCH, NULL, NULL, "dxvk-for-new-games", 0, NULL, 1, 2, OFF_ON,
        UI_TEXT( "New applications start with DXVK", "Новым приложениям назначать DXVK", "新应用默认使用 DXVK" ),
        UI_TEXT( "The renderer an application gets when it is added to the library. Off gives it Wine's.",
               "Рендер, который получает приложение при добавлении в библиотеку. Выключено — рендер Wine.", "向游戏库添加应用时使用的渲染器。关闭后默认使用 Wine 渲染器。" ),
        { NULL, NULL } },

    [APP_SET_LANGUAGE] = { APP_SET_LANGUAGE, APP_GROUP_SHELL, APP_TYPE_CHOICE, NULL, NULL, "language", 0, NULL, 0, 4,
        { { "system", NULL, UI_TEXT( "As the console", "Как в консоли", "跟随主机" ) },
          { "en", NULL, UI_TEXT( "English", "English", "English" ) },
          { "ru", NULL, UI_TEXT( "Русский", "Русский", "Русский" ) },
          { "zh-CN", NULL, UI_TEXT( "简体中文", "简体中文", "简体中文" ) } },
        UI_TEXT( "Language", "Язык", "语言" ),
        UI_TEXT( "The language of the interface.", "Язык интерфейса.", "界面显示语言。" ),
        { NULL, NULL } },

    [APP_SET_ANIMATIONS] = { APP_SET_ANIMATIONS, APP_GROUP_SHELL, APP_TYPE_SWITCH, NULL, NULL, "animations", 0, NULL, 1, 2, OFF_ON,
        UI_TEXT( "Animations", "Анимации", "界面动画" ),
        UI_TEXT( "Transitions between screens and the movement of the focus.",
               "Переходы между экранами и движение фокуса.", "页面切换和焦点移动时的动画。" ),
        { NULL, NULL } },

    [APP_SET_SOUNDS] = { APP_SET_SOUNDS, APP_GROUP_SHELL, APP_TYPE_SWITCH, NULL, NULL, "sounds", 0, NULL, 1, 2, OFF_ON,
        UI_TEXT( "Interface sounds", "Звуки интерфейса", "界面音效" ),
        UI_TEXT( "The sounds of moving, choosing and going back.", "Звуки перемещения, выбора и возврата.", "移动、选择和返回时的音效。" ),
        { NULL, NULL } },
};

const struct app_setting_def *app_setting_def( enum app_setting id )
{
    return id >= 0 && id < APP_SET_COUNT ? &defs[id] : NULL;
}

const char *app_setting_group_name( enum app_setting_group group, enum app_language language )
{
    return group_names[group][language];
}

const char *app_setting_layer_name( enum app_setting_layer layer, enum app_language language )
{
    return layer_names[layer][language];
}

const char *app_setting_reason_text( enum app_setting_reason reason, enum app_language language )
{
    return reason_texts[reason][language];
}

/* The choice a word in a file names, or -1. */
static int choice_of( const struct app_setting_def *def, const char *word )
{
    int i;

    for (i = 0; i < def->choice_count; i++)
        if ((def->choices[i].value && !strcasecmp( def->choices[i].value, word )) ||
            (def->choices[i].alias && !strcasecmp( def->choices[i].alias, word ))) return i;
    return -1;
}

/* What the program's own file says: the key, or its older name when the key is
 * not there at all. */
static int profile_word( const struct launcher_kv *profile, const struct app_setting_def *def,
                         char *word, size_t size )
{
    if (!profile || !def->profile_key) return 0;
    if (launcher_kv_get( profile, def->profile_key, word, size )) return 1;
    return def->profile_alias && launcher_kv_get( profile, def->profile_alias, word, size );
}

static int global_choice( const struct wine_nx_config *global, const struct app_setting_def *def )
{
    char word[64];
    int i;

    if (!global || !def->global_key || (i = wine_nx_config_find( global, def->global_key )) < 0) return -1;
    if (def->type == APP_TYPE_SWITCH && global->entries[i].value[0] != '"')
    {
        int on = wine_nx_config_bool( global, def->global_key, -1 );

        if (on < 0) return -1;
        return def->global_inverted ? !on : on;
    }
    if (def->type == APP_TYPE_CHOICE && def->global_inverted && global->entries[i].value[0] != '"')
    {
        int on = wine_nx_config_bool( global, def->global_key, -1 );

        return on < 0 ? -1 : !on;
    }
    if (global->entries[i].value[0] == '"')
    {
        if (!wine_nx_config_string_value( global, def->global_key, word, sizeof(word) )) return -1;
    }
    else snprintf( word, sizeof(word), "%s", global->entries[i].value );
    return choice_of( def, word );
}

/* A loose test file. Most hold a truth value; stall-snapshots.txt switches off
 * only with "0", and the two that hold a number keep it as it was written. */
static int experiment_choice( const struct app_setting_def *def, app_settings_experiment experiment,
                              void *context, char *raw, size_t raw_size )
{
    char line[64];

    raw[0] = 0;
    if (!experiment || !def->experiment_file || !experiment( context, def->experiment_file, line, sizeof(line) ))
        return -1;
    if (def->id == APP_SET_STALL_SNAPSHOTS) return strcmp( line, "0" ) != 0;
    if (def->type == APP_TYPE_SWITCH)
        return !strcmp( line, "1" ) || !strcasecmp( line, "true" ) || !strcasecmp( line, "yes" ) ||
               !strcasecmp( line, "run" );
    snprintf( raw, raw_size, "%.*s", (int)raw_size - 1, line );
    return choice_of( def, line );
}

static void apply_limit( struct app_setting_value *value, int choice, enum app_setting_reason reason )
{
    if (value->effective == choice) return;
    value->layer[APP_LAYER_LIMIT] = choice;
    value->effective = choice;
    value->source = APP_LAYER_LIMIT;
    value->reason = reason;
}

void app_settings_resolve( struct app_settings *out, const struct wine_nx_config *global,
                           const struct launcher_kv *profile, const struct app_settings_limits *limits,
                           app_settings_experiment experiment, void *context )
{
    static const struct app_settings_limits no_limits = { 0, 1, 1, 1, 1, 0 };
    static const enum app_setting intrusive[] = { APP_SET_VERBOSE, APP_SET_PROFILER, APP_SET_WAIT_DIAGNOSTICS };
    static const enum app_setting passive[] = { APP_SET_SESSION_LOGS, APP_SET_PERF_COUNTERS,
                                                APP_SET_STALL_SNAPSHOTS, APP_SET_REMOTE_SNAPSHOT };
    struct app_setting_value *v;
    char word[64], raw[APP_SETTING_TEXT];
    unsigned int n;
    int id, layer;

    if (!limits) limits = &no_limits;
    memset( out, 0, sizeof(*out) );
    for (id = 0; id < APP_SET_COUNT; id++)
    {
        const struct app_setting_def *def = &defs[id];

        v = &out->values[id];
        for (layer = 0; layer < APP_LAYER_COUNT; layer++) v->layer[layer] = -1;
        v->layer[APP_LAYER_DEFAULT] = def->default_choice;
        if (id == APP_SET_OWN_CONTROLS) v->layer[APP_LAYER_DEFAULT] = !!limits->has_own_keys;
        v->layer[APP_LAYER_GLOBAL] = global_choice( global, def );
        if (profile_word( profile, def, word, sizeof(word) ))
        {
            if (def->type == APP_TYPE_TEXT)
            {
                if (launcher_dxvk_version_valid( word ))
                {
                    snprintf( v->chosen_text, sizeof(v->chosen_text), "%.*s", (int)sizeof(v->chosen_text) - 1, word );
                    v->layer[APP_LAYER_PROFILE] = 0;
                }
            }
            else v->layer[APP_LAYER_PROFILE] = choice_of( def, word );
        }
        for (layer = APP_LAYER_PROFILE; layer >= APP_LAYER_DEFAULT; layer--)
            if (v->layer[layer] >= 0) break;
        v->effective = v->layer[layer];
        v->source = layer;
        if (def->type == APP_TYPE_TEXT) memcpy( v->text, v->chosen_text, sizeof(v->text) );
    }

    /* What a combination of choices rules out, in the order the runtime has
     * always applied it: the renderer the program asked for decides whether
     * its DXVK variant and version can stand. */
    if (out->values[APP_SET_RENDERER].effective == 2)
    {
        struct app_setting_value *source = &out->values[APP_SET_DXVK_SOURCE];
        struct app_setting_value *version = &out->values[APP_SET_DXVK_VERSION];

        if (source->effective == 1 /* Sarek */ ||
            (version->text[0] && !launcher_dxvk_vkd3d_compatible( (enum dxvk_source)source->effective, version->text )))
        {
            apply_limit( source, 0, APP_REASON_VKD3D_NEEDS_OFFICIAL );
            if (version->text[0])
            {
                version->text[0] = 0;
                version->layer[APP_LAYER_LIMIT] = 0;
                version->source = APP_LAYER_LIMIT;
                version->reason = APP_REASON_VKD3D_NEEDS_OFFICIAL;
            }
        }
    }
    if (!limits->vulkan) apply_limit( &out->values[APP_SET_RENDERER], 0, APP_REASON_NO_VULKAN );
    if (!limits->lsfg_installed) apply_limit( &out->values[APP_SET_LSFG], 0, APP_REASON_LSFG_MISSING );

    /* Diagnostics are a developer's: without the mode there are none. */
    if (out->values[APP_SET_DEVELOPER].effective <= 0)
        apply_limit( &out->values[APP_SET_DIAGNOSTICS_MODE], 0, APP_REASON_DEVELOPER_OFF );

    /* Extended diagnostics alone may change how the program runs. */
    if (out->values[APP_SET_DIAGNOSTICS_MODE].effective < 2)
        for (n = 0; n < sizeof(intrusive) / sizeof(intrusive[0]); n++)
            apply_limit( &out->values[intrusive[n]], 0, APP_REASON_NEEDS_EXTENDED );

    /* A test file put on the card from a computer goes over all of that. */
    for (id = 0; id < APP_SET_COUNT; id++)
    {
        int choice = experiment_choice( &defs[id], experiment, context, raw, sizeof(raw) );

        v = &out->values[id];
        if (choice < 0 && !raw[0]) continue;
        v->layer[APP_LAYER_EXPERIMENT] = choice;
        v->effective = choice;
        v->source = APP_LAYER_EXPERIMENT;
        v->reason = APP_REASON_EXPERIMENT_FILE;
        memcpy( v->text, raw, sizeof(v->text) );
    }

    /* Switched off as a whole means off: nothing above brings a part of it back. */
    if (out->values[APP_SET_DIAGNOSTICS_MODE].effective == 0)
    {
        enum app_setting_reason why = out->values[APP_SET_DEVELOPER].effective <= 0 ? APP_REASON_DEVELOPER_OFF
                                                                                    : APP_REASON_DIAGNOSTICS_OFF;

        for (n = 0; n < sizeof(intrusive) / sizeof(intrusive[0]); n++)
            apply_limit( &out->values[intrusive[n]], 0, why );
        for (n = 0; n < sizeof(passive) / sizeof(passive[0]); n++)
            apply_limit( &out->values[passive[n]], 0, why );
    }
}

const char *app_setting_value_label( const struct app_settings *settings, enum app_setting id,
                                     enum app_language language )
{
    const struct app_setting_def *def = &defs[id];
    const struct app_setting_value *value = &settings->values[id];

    if (def->type == APP_TYPE_TEXT || value->effective < 0 || value->effective >= def->choice_count)
        return value->text;
    return def->choices[value->effective].label[language];
}

int app_settings_set_profile( struct launcher_kv *profile, enum app_setting id, int choice, const char *text )
{
    const struct app_setting_def *def = app_setting_def( id );
    const char *word = NULL;

    if (!def || !def->profile_key) return 0;
    if (def->profile_alias && !launcher_kv_set( profile, def->profile_alias, NULL )) return 0;
    if (def->type == APP_TYPE_TEXT)
    {
        if (choice >= 0 && text && text[0])
        {
            if (!launcher_dxvk_version_valid( text )) return 0;
            word = text;
        }
    }
    else if (choice >= def->choice_count) return 0;
    /* A setting no other layer can give is left out when it is at its default,
     * which is how these files have always been written. */
    else if (choice >= 0 && (def->global_key || choice != def->default_choice || id == APP_SET_OWN_CONTROLS))
        word = def->choices[choice].value ? def->choices[choice].value : def->choices[choice].alias;
    return launcher_kv_set( profile, def->profile_key, word );
}

int app_settings_set_global( struct wine_nx_config *global, enum app_setting id, int choice )
{
    const struct app_setting_def *def = app_setting_def( id );
    char json[80];

    if (!def || !def->global_key || choice < 0 || choice >= def->choice_count) return 0;
    if (def->type == APP_TYPE_SWITCH || def->global_inverted)
        return wine_nx_config_set_bool( global, def->global_key, def->global_inverted ? !choice : choice );
    snprintf( json, sizeof(json), "\"%s\"", def->choices[choice].value );
    return wine_nx_config_set( global, def->global_key, json );
}

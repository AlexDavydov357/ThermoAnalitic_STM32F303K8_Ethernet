# Прошивка FZK_THERMO_LAMPSHUTTER 2.0: установка

## Что в папке

- `FZK_firmware_v2.0.zip`: изменённые и новые файлы проекта, пути от корня проекта (`Core/...`).
- `src/`: те же файлы, распакованные.
- `FZK_v2.0_test_build.hex`: моя проверочная сборка (arm-none-eabi-gcc 13.2, -O0). Прошивать лучше то, что соберёт CubeIDE.
- `PROTOCOL_v2.md`: изменения протокола для MobiTherm.
- `tests/`: тесты логики заслонки, ламп и разбора команд на ПК (gcc, без железа).

## Установка

1. Закрыть проект в STM32CubeIDE.
2. Распаковать `FZK_firmware_v2.0.zip` в `D:\_Activtest\FZK_THERMO_LAMPSHUTTER_v1` с заменой файлов. PowerShell:
   `Expand-Archive -Force FZK_firmware_v2.0.zip D:\_Activtest\FZK_THERMO_LAMPSHUTTER_v1`
3. Открыть проект, Project → Clean, затем Build. Новые `.c` файлы в `Core/Src` CubeIDE подхватит сам.
4. Отключить силовое питание мотора и ламп, прошить, включить.
5. Проверить по порядку: `VERSION;`, `STATUS;`, `SHUT 0;` / `SHUT 1;` до концевиков, `SHUT 2;` на ходу, `TIM 3;` + `FLASH 1;`, `FLASH 0;` во время нагрева.

## Откат

`STM32_Programmer_CLI -c port=SWD -w backup\FZK_dump_2026-10-06.bin 0x08000000 -v -rst`

Дамп совпадает с `Debug\FZK_THERMO_LAMPSHUTTER_v1.elf` сборки 2025-10-10 (отличаются только 8 байт заполнения по адресам 0x188–0x18F).

## CubeMX

Файл `.ioc` я не менял. Всё, что отличается от настроек CubeMX (ШИМ PWM1 с низкой полярностью, подтяжки на концевиках, приём UART), сделано внутри блоков USER CODE и в новых файлах, поэтому повторная генерация кода ничего не сотрёт.

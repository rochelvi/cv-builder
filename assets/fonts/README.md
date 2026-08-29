# Приложенный шрифт

`LiberationSans-Regular.ttf` и `LiberationSans-Bold.ttf` — Liberation Sans 2.1.5,
без изменений, из пакета `fonts-liberation` (upstream:
<https://github.com/liberationfonts/liberation-fonts>). Лицензия — SIL Open Font
License 1.1, текст в `LICENSE-LiberationSans.txt`; она совместима с
распространением вместе с GPL-3.0 программой и не требует ничего, кроме
сохранения этого уведомления.

## Зачем шрифт лежит в репозитории

Переносы строк в шаблоне — функция ширин символов того шрифта, которым резюме
размечено. Возьми другой шрифт — переносы сдвинутся, за ними сдвинутся базовые
линии, и документ может занять другое число страниц. Поэтому шрифт не может
выбираться системой: иначе PDF, собранный на Windows, Linux и macOS, будет тремя
разными файлами, а резюме, размеченное в одной версии программы, «поедет» в
другой.

Приложенный шрифт пробуется первым на всех платформах, системные — только как
резерв (см. `src/app/fonts.cpp`).

## Почему именно Liberation Sans

Шаблон построен на метриках Helvetica; на Windows его держал Arial, метрически
совпадающий с ней. Liberation Sans выбран не по репутации «метрически
совместимого с Arial», а по измерению — `cvcli --font-report` печатает ширину
каждого символа, а `tools/fontprobe.sh` сравнивает раскладку целиком:

| Шрифт | Ширины символов против Arial | Раскладка `sample_cv.json` |
| --- | --- | --- |
| **Liberation Sans** | совпадают во всех 918 кодовых точках | **идентична** |
| Arimo | совпадают во всех 918 кодовых точках | идентична |
| DejaVu Sans | расходятся в 896 из 918 | 6 лишних строк переноса |

Проверялись ASCII, Latin-1, весь блок кириллицы (U+0400–U+04FF) и типографские
знаки, которые ставит сам движок раскладки (`›`, `·`, тире, кавычки, многоточие).
Непокрытых символов ни у одного кандидата нет.

Отсюда два следствия: PDF одинаков на трёх системах, и существующие резюме,
размеченные прежней Windows-версией на Arial, продолжают размечаться точно так
же — приложенный шрифт не меняет ни одной строки.

Повторить проверку:

```bash
cmake --preset linux && cmake --build build/linux -j

# сравнение раскладки целиком
tools/fontprobe.sh build/linux/bin/cvcli \
    arial=/path/to/arial.ttf,/path/to/arialbd.ttf \
    liberation=assets/fonts/LiberationSans-Regular.ttf,assets/fonts/LiberationSans-Bold.ttf

# ширины символов по одному, для диффа
build/linux/bin/cvcli --font-report --fonts /path/to/arial.ttf /path/to/arialbd.ttf > arial.txt
build/linux/bin/cvcli --font-report > bundled.txt
diff <(grep -v '^\[' arial.txt) <(grep -v '^\[' bundled.txt) && echo "метрики совпадают"
```

Arial при этом берётся с машины и в репозиторий не копируется: его лицензия
этого не разрешает. Кандидаты для сравнения удобно доставать из пакетов
дистрибутива — это upstream-сборка с приложенным файлом лицензии, и sudo не
нужен:

```bash
apt-get download fonts-liberation fonts-croscore
for deb in *.deb; do dpkg-deb -x "$deb" ext; done
```

Результат сравнения зафиксирован в тестах: `tests/test_font.cpp` держит таблицу
ширин (её печатает `tools/metricstable.sh` из любого шрифта), так что подмена
файла шрифта на не совпадающий по метрикам роняет тесты, а не тихо меняет
вёрстку всех резюме.

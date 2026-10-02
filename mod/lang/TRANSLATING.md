# Translating Nitoryu / Перевод Nitoryu

*(Русский ниже.)*

## English

Nitoryu has only a few texts: two speech lines and the Dual Wielding line in the stats window. Translating it takes a few minutes.

1. **Copy** `en.ini` in this folder to `<code>.ini`, where `<code>` is your language's two-letter code: `de.ini`, `fr.ini`, `pl.ini`, `zh.ini`...
   - It is the first part of `language=` in `Kenshi\settings.cfg`: `de_DE` → `de`.
2. **Translate** the text after each `=`.
   - Keep the key before `=` unchanged.
   - Keep anything in `{curly braces}` as it is.
   - `\n` makes a line break.
   - Lines starting with `;` are notes for you; they may stay in English.
3. **Save as UTF-8.** In Notepad: Save As → Encoding: UTF-8.
4. **Check it** (optional; needs Python 3): open a command prompt in this folder and run `python lang_check.py <code>.ini`.
   - It reports missing keys, typos in keys, texts too long for their place, and a wrong encoding.
5. **Use it.**
   - Nitoryu picks the game's language automatically.
   - To force one, set `language=<code>` under `[General]` in `Nitoryu.ini` (`auto` = the game's language).
6. **Share it:** send the file to the mod's author, or publish a tiny mod that only contains `Nitoryu\lang\<code>.ini`.

Missing or empty texts show in English, so a partial translation is safe. Your file stays in `lang\`; it is not overwritten by `Nitoryu.ini` updates, but a new Nitoryu version may bring new keys. Run the checker again then.

## Русский

Текстов в Nitoryu немного: две реплики и строка «Парный бой» в окне характеристик. Перевод займёт несколько минут.

1. **Скопируйте** `en.ini` из этой папки в `<код>.ini`, где `<код>` это двухбуквенный код языка: `de.ini`, `fr.ini`, `pl.ini`, `uk.ini`...
   - Это первая часть строки `language=` в `Kenshi\settings.cfg`: `uk_UA` → `uk`.
2. **Переведите** текст после каждого `=`.
   - Ключ до `=` не меняйте.
   - Всё в `{фигурных скобках}` оставьте как есть.
   - `\n` это перенос строки.
   - Строки, начинающиеся с `;`, это пояснения; их можно не переводить.
3. **Сохраните в UTF-8.** В Блокноте: «Сохранить как» → Кодировка: UTF-8.
4. **Проверьте** (по желанию, нужен Python 3): откройте командную строку в этой папке и выполните `python lang_check.py <код>.ini`.
   - Проверка покажет пропущенные ключи, опечатки в ключах, слишком длинные тексты и неверную кодировку.
5. **Использование.**
   - Nitoryu сам берёт язык игры.
   - Чтобы выбрать язык вручную, укажите `language=<код>` в разделе `[General]` файла `Nitoryu.ini` (`auto` = язык игры).
6. **Поделитесь:** отправьте файл автору мода или выложите крошечный мод, в котором только `Nitoryu\lang\<код>.ini`.

Пропущенные или пустые тексты показываются по-английски, так что неполный перевод безопасен. Новая версия Nitoryu может добавить ключи; тогда запустите проверку ещё раз.

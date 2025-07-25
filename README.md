/*  Utility/Daemon for meta data generation
Утилита, которая сгенерирует мета данные о каталоге / вычислит дельту до самой актуальной версии файлов
1. full-meta-"date".XML - содержит полную информацию структуре каталога.
2. delta-meta-"date".XML - содержит дельту изменений между указанным актуальным full-meta файлами
Принимает аргументы
[1]-full / %version% полные или дельта, обязательно (во втором случае строка - версия относительно которой формируется дельта)
[2]-target_dir путь к каталогу для генерации, обязательно (для дельты в нем должны находится full-meta документы а не каталог для генерации)
[3]-meta_dir путь к каталогу сохранения XML, по умолчанию target_dir (не обязательно)

Демон предназначен для автогенерации full-meta, отслеживает изменения в target-dir.
Демон предназначен для генерации delta-meta по запросу (TCP) и отправки клиенту
Будет автом. генерировать meta данные, delta по запросу
Принимает аргументы:
[1]-full/delta как будет отрабатывать, обязательно
[2]-target_dir путь к каталогу для генерации, обязательно (для дельты в нем должны находится full-meta документы а не каталог для генерации)
[3]-meta путь сохранения
[4]-demonize
[5]-dir сохранения снэпов версий (для full) / адрес в формате 1111.2222.3333.444 (для delta)
[6]-Порт прослушивания (для delta обязательно)
*/

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - ПРОТОКОЛ - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
    ================================
    Протокол взаимодействия UNET-MES
    ================================

    Клиент ↔ Сервер: пошаговый, текстово-бинарный протокол для дифференцированной передачи файлов и мета-обновлений.

    ---------- Этап 1: Инициализация ----------
    // 1.1 Клиент объявляет поддержку протокола
    UNET-MES

    // 1.2 Клиент запрашивает обновление
    GETUPDATE

    // 1.3 Клиент передаёт текущую версию
    %version%               // Например: 1.4.2

    ---------- Этап 2: Ответ сервера ----------
    NOUPDATE                // Обновление не требуется
    SOMEUPDATE              // Есть обновление → переход к ожиданию согласия

    ---------- Этап 3: Решение клиента ----------
    AGREE                   // Клиент готов принять обновление
    REJECT                  // Клиент отказывается от обновления (сеанс завершается)

    ---------- Этап 4: Передача обновления ----------

    VERSION:%version%       // Новая актуальная версия (например: 1.5.0)

    // Далее возможны следующие команды от сервера:

    NEWDIR:%path%           // Создать новый каталог

    NEWFILE:%path%          // Новый файл
    uint32_t %weight%       // Размер файла (в байтах), big-endian

    // 4.1 После отправки заголовка и размера, сервер ждёт подтверждения:
    OK                      // Клиент готов принимать файл
    ERROR                   // Ошибка на стороне клиента — файл не будет передан (или будет позже)

    // 4.2 Только после получения OK сервер передаёт бинарное тело файла:
    [binary body]           // Ровно %weight% байт

    DELFILE:%path%          // Удалить указанный файл (клиент обязан удалить)
    DELDIR:%path%           // Удалить указанный каталог (рекурсивно)

    VERSION:%version%       // Повторное указание новой версии (финальное подтверждение)

    COMPLETE                // Обновление завершено успешно

    ---------- Этап 5: Ошибки ----------
    SERVERERROR             // Внутренняя ошибка сервера — клиент завершает соединение

    ===============================
    Примечания:
    - Пути (`%path%`) в POSIX-формате, без ведущих слэшей.
    - `%weight%` передаётся как 4 байта в формате big-endian.
    - После `NEWFILE` и размера клиент должен отправить `OK` перед получением тела файла.
    - Команды `DELFILE` и `DELDIR` обязательны к выполнению клиентом.
    - `COMPLETE` — маркер завершения обмена.
*/

- - - - - - - - - - - - - - - - - - - - - - - - - - - - ДЕПЛОИНГИН НА СЕРВАКЕ- - - - - - - - - - - - - - - - - - - - - -

Размещение утилиты (исполняемого файла) в /etc/metad
Таргет full-meta в /home/uniter/
Таргет delta-meta в /home/uniterfull/
Мета delta-meta d /home/uniterdelta/
Для логирования, создать файл /var/log/metad/logfile.log !!!
Добавить пользователя uniter-meta, передать полные права на папки (сделать владельцем) /home/uniter, /home/uniterfull, /home/uniterdelta, /etc/metad

/*Последовательность команд
[1]Создаем логфайлы
sudo mkdir -p /var/log/metad
sudo touch /var/log/metad/fullmetalog.log
sudo touch /var/log/metad/deltametalog.log
[2]Создание пользователя uniter-meta
sudo useradd --system --no-create-home --shell /usr/sbin/nologin uniter-meta
[3]С помощью системы ACL добавляем ему права на папки в все файлы, в т.ч. и в будущем
sudo setfacl -R -m u:uniter-meta:rwx /home/uniter/
sudo setfacl -R -m u:uniter-meta:rwx /home/uniterfull/
sudo setfacl -R -m u:uniter-meta:rwx /home/uniterdelta/
sudo setfacl -R -m u:uniter-meta:rwx /var/log/metad/
[4]Скачиваем библиотеку tinyxml2
sudo apt update
sudo apt install libtinyxml2-dev

[5]Далее создаем файлы конфигурации для systemctl
[5.1]sudo nano /etc/systemd/system/fullmetad.service

[Unit]
Description=FullMeta Daemon
After=network.target

[Service]
Type=simple
User=uniter-meta
Group=uniter-meta
ExecStart=/etc/metad/metad full /home/uniter/ /home/uniterfull/ demonize
Restart=on-failure

# Логирование прямо в файл
StandardOutput=append:/var/log/metad/fullmetalog.log
StandardError=append:/var/log/metad/fullmetalog.log

[5.2]sudo nano /etc/systemd/system/deltametad.service

[Unit]
Description=DeltaMeta Daemon
After=network.target

[Service]
Type=simple
User=uniter-meta
Group=uniter-meta
ExecStart=/etc/metad/metad delta /home/uniterfull/ /home/uniterdelta/ demonize 77.110.116.155 6666
Restart=on-failure

# Логирование прямо в файл
StandardOutput=append:/var/log/metad/fullmetalog.log
StandardError=append:/var/log/metad/fullmetalog.log

[6]Запускаем с помощью systemctl
sudo systemctl daemon-reload
sudo systemctl enable fullmetad
sudo systemctl start fullmetad
sudo systemctl enable deltametad
sudo systemctl start deltametad

[7]Проверяем
sudo systemctl status fullmetad
sudo systemctl status deltametad
*/

Запуск от имени uniter-meta с помощью systemctl
##full-meta демон
sudo -u uniter-meta /etc/metad/metad full /home/uniter/ /home/uniterfull/ demonize

##delta-meta  демон
sudo -u uniter-meta /etc/metad/metad delta /home/uniterfull/ /home/uniterdelta/ demonize %IPv4% %port%

##full-meta утилита
sudo -u uniter-meta /etc/metad/metad full /home/uniter/ /home/uniterfull/

#delta-meta утилита
sudo -u uniter-meta /etc/metad/metad %version% /home/uniterfull/ /home/uniterdelta/

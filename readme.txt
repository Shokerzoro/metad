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
[5]-IPv4 адрес в формате 1111.2222.3333.444 (для delta обязательно)
[6]-Порт прослушивания (для delta обязательно)
*/

Пакет данных (любой) состоит из
[1]uint8_t - один байт для хранения размера хэдера
[2]header - какое то кол-во байт = uint8_t для хранения хэдэра в ascii вместе с терминальным нулем
[3]uint32_t - 4 байта для хранения длины тела (опционально)
[4]body - само тело размером size_t(uint32_t) - бинарные данные (опционально)

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - ДЕПЛОИНГИН НА СЕРВАКЕ- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

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

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - ПРОТОКОЛ - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//Запрос на актуальность версии
MYVERSION:%version% версия файлов
//Согласие на получение обновления
GETUPDATE
###Ответы от сервака
//Версия актуальна
NOUPDATE
//Есть обновления
SOMEUPDATE
//Новый каталог (нужно создать)
NEWDIR:%path%
//Новый файл (нужно создать)
NEWFILE:%path%
uint32_t %weight%
body binary
//Удалить файл
DELFILE:%path%
//Удалить каталог
DELDIR:%path%
//Обновление версии
VERSION:%version%
//Ошибка, прекращение обновления
SERVERERROR

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - ПРОБЛЕМЫ - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -


В идеале добавить свои классы exceptions для ошибок протокола. Совсем другая причина возникновения и логика обработки.
Добавить блокировку файлов на время ожидания таймера full-meta
//файлы обновлены, но fullmeta еще не сгенерирована; можем отправить файл, который обновлен, но мы о нем ничего не знаем.
//В теории, если одновременно почти пришел запрос, и происходит генерация delta. Можно не сохранить файл, т.к. он только что сохранился другим потоком.
Добавить время ожидания на чтение из сокета
//В сетях пишут, что все равно ОС рано или поздно закроет сокет и вернет -1, но мб не стоит долго ждать
Проверка регистрации - тут нужно подключать базу данных уже
Добавить шифрование над TCP - можно использовать открытые библиотеки


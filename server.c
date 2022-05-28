#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <errno.h>
#include <sys/timerfd.h>
#include <pthread.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/ipc.h>

enum Constants {
    //максимальный размер udp дейтаграммы
    MAX_UDP_SIZE = 65507,
    //количество epoll событий (тут 2 - сработка таймера и приход строки в дейтаграмме)
    MAX_EPOLL_EVENTS = 2,
    //для семафоров
    MODE = 0666,
    KEY = 123
};

static int udp_socket, smid;
//по умолчанию количество тредов = 0
static int thread_num = 0;
//статистика запросов и ответов
static long long int requests = 0;
static long long int requests_s = 0;
static long long int requests_p = 0;
static long long int sendings = 0;
static long long int sendings_s = 0;
static long long int sendings_p = 0;

//вспомогательная функция вычисления длины числа в 10 сс
static int 
length_of_number(int num) 
{
    int res = 0;
    while (num != 0) {
        num /= 10;
        res++;
    }
    return res;
}

//просеивание для пирамидальной сортировки
static void 
sift(int *a, int root, int bottom)
{
    int max_child;
    int done = 0;
    while ((root * 2 <= bottom) && (done != 1)) {
        if (root * 2 == bottom) {
            max_child = root * 2;
        } else if (a[root * 2] > a[root * 2 + 1]) {
            max_child = root * 2;
        } else {
            max_child = root * 2 + 1;
        }
        if (a[root] < a[max_child]) {
            int tmp = a[root];
            a[root] = a[max_child];
            a[max_child] = tmp;
            root = max_child;
        } else {
            done = 1;
        }
    }
}

//сортировка
static void 
heap_sort(int *a, int size) 
{
    for (int i = (size / 2); i >= 0; i--) {
        sift(a, i, size - 1);
    }
    for (int i = size - 1; i >= 1; i--) {
        int tmp = a[0];
        a[0] = a[i];
        a[i] = tmp;
        sift(a, 0, i - 1);
    }
}

//установка таймера
//при каждом выводе статистики таймер начинается с нуля (иначе его epoll будет читать бесконечно много раз)
static void set_timer(int timer_fd, int timeout_sec) 
{
    struct itimerspec timeout;
    memset(&timeout, 0, sizeof(struct itimerspec));
    timeout.it_value.tv_sec = timeout_sec;
    if(timerfd_settime(timer_fd, 0, &timeout, NULL) == -1) {
        fprintf(stderr, "Timeot error!\n");
        _exit(1);
    }
}

//отправка сообщения на 127.0.0.1
//дополнительных действий с epoll тут вроде как не требуется, потому что udp не может отправить только часть сообщения, как в случае с tcp
static void
send_to_dst(int udp_socket, char *dst_buffer, int port) 
{
    struct sockaddr_in dst_addr;
    memset(&dst_addr, 0, sizeof(struct sockaddr_in));
    dst_addr.sin_family = PF_INET;
    dst_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    dst_addr.sin_port = port;
    if (sendto(udp_socket, (const char *)dst_buffer, strlen(dst_buffer), MSG_DONTWAIT, (const struct sockaddr *)&dst_addr, sizeof(dst_addr)) == -1) {
        fprintf(stderr, "Send error!\n");
    }
}

//функция обработки строк
static void*
function(void *arg) {
    //статистика в рамках одного треда
    int local_requests = 0;
    int local_requests_s = 0;
    int local_requests_p = 0;
    int local_sendings = 0;
    int local_sendings_s = 0;
    int local_sendings_p = 0;
    char dst_buffer[MAX_UDP_SIZE];
    //это не указатель на буфер, это скопированная строка, чтобы несколько работающих тредов не мешали друг другу!
    const char *buffer = (const char*)arg;
    //массив в который переводится строка
    int *a = calloc(101, sizeof(int));
    local_requests++;
    //мод - сортировка или подсчет суммы
    char mode = 0;
    //длина уже считанного сообщения (сначала 1 - тк считан только мод)
    int len = 1;
    //порт для отправки сообщения на 127.0.0.1, если -1 то не отправляем
    int port = -1;
    char *tmp;
    if ((tmp = strstr(buffer, ";")) != NULL) {
        port = strtol(tmp + 1, NULL, 10);
    }
    int size;
    //считываем мод
    sscanf(buffer, "%c", &mode);
    //считываем сообщение в массив
    for (size = 0; sscanf(buffer + len, "%d", &a[size]) != EOF; size++) {
        if (tmp != NULL) {
            if (buffer + len >= tmp) {
                break;
            }
        }
        len += length_of_number(a[size]) + 1;
        if (size % 100 == 0 && size != 0) {
            a = realloc(a, (size + 101) * sizeof(int));
        }
    }
    //если нужно сортировать
    if (mode == 's') {
        local_requests_s++;
        heap_sort(a, size);
        len = 0;
        for (int i = 0; i < size; i++) {
            sprintf(dst_buffer + len, "%d ", a[i]);
            len += length_of_number(a[i]) + 1;
        }
        printf("%s\n", dst_buffer);
        //если нужно отправить на 127.0.0.1
        if (port != -1) {
            send_to_dst(udp_socket, dst_buffer, port);
            local_sendings++;
            local_sendings_s++;
        }
    }
    //если нужно считать сумму
    if (mode == 'p') {
        local_requests_p++;
        long long int res = 0;
        for (int i = 0; i < size; i++) {
            res += a[i];
        }
        sprintf(dst_buffer, "%lld", res);
        printf("%s\n", dst_buffer);
        //если нужно отправить на 127.0.0.1
        if (port != -1) {
            send_to_dst(udp_socket, dst_buffer, port);
            local_sendings++;
            local_sendings_p++;
        }
    }
    //для синхронизации с общей статистикой используем ipc семафор
    struct sembuf sem;
    sem.sem_num = 0;
    //опускаем - можно записать статистику, другие треды ждут
    sem.sem_op = -1;
    sem.sem_flg = 0;
    if (thread_num != 0) {
        if (semop(smid, &sem, 1) == -1) {
            fprintf(stderr, "Semop error!\n");
            _exit(1);
        }
    }
    //записываем статистику
    requests += local_requests;
    requests_s += local_requests_s;
    requests_p += local_requests_p;
    sendings += local_sendings;
    sendings_s += local_sendings_s;
    sendings_p += local_sendings_p;
    //поднимаем - даем доступ на запись другим тредам
    sem.sem_op = 1;
    if (thread_num != 0) {
        if (semop(smid, &sem, 1) == -1) {
            fprintf(stderr, "Semop error!\n");
            _exit(1);
        }
    }
    free(a);
    return NULL;
}

int
main(int argc, char **argv)
{
    //создаем дескриптор таймера
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    //создаем дескриптор epoll
    int ep_fd = epoll_create(MAX_EPOLL_EVENTS);
    int timeout_sec = 0;
    //обработка аргументов -p port обязательно 1 аргументом, остальное не важно в каком порадке
    if (argv[3] != NULL && argv[4] != NULL && strcmp(argv[3], "-w") == 0) {
        timeout_sec = strtol(argv[4], NULL, 10);
    }
    if (argv[5] != NULL && argv[6] != NULL && strcmp(argv[5], "-w") == 0) {
        timeout_sec = strtol(argv[6], NULL, 10);
    }
    if (argv[3] != NULL && argv[4] != NULL && strcmp(argv[3], "-t") == 0) {
        //если работаем с тредами, то создаем семафор
        thread_num = strtol(argv[4], NULL, 10);
        smid = semget(KEY, 1 , IPC_CREAT | MODE);
    }
    if (argv[5] != NULL && argv[6] != NULL && strcmp(argv[5], "-t") == 0) {
        //если работаем с тредами, то создаем семафор
        thread_num = strtol(argv[6], NULL, 10);
        smid = semget(KEY, 1, IPC_CREAT | MODE);
    }
    pthread_t threads[thread_num];
    //открываем udp сокет
    if ((udp_socket = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
        fprintf(stderr, "Socket error!\n");
        _exit(1);
    }
    //переводим сокет и таймер в неблокирующий режим
    fcntl(timer_fd, F_SETFL, O_NONBLOCK);
    fcntl(udp_socket, F_SETFL, O_NONBLOCK);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = PF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    if (argv[1] == NULL || argv[2] == NULL || strcmp(argv[1], "-p") != 0) {
        printf("Please enter port value -p PORT!\n");
        _exit(0);
    }
    addr.sin_port = htons(strtol(argv[2], NULL, 10));
    //привязываем сокет к адресу
    if (bind(udp_socket, (const struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1) {
        fprintf(stderr, "Bind error!\n");
        _exit(1);
    }
    //первый раз устанавливем таймер
    set_timer(timer_fd, timeout_sec);
    
    //добавляем к событиям epoll получение дейтаграммы на сокете
    struct epoll_event ep;
    memset(&ep, 0, sizeof(struct epoll_event));
    ep.events = EPOLLIN;
    ep.data.fd = udp_socket;
    if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, udp_socket, &ep) == -1) {
        fprintf(stderr, "Epoll socket error!\n");
        _exit(1);
    }
    
    //добавляем к событиям epoll возможность читать из тиаймера (по таймауту)
    ep.events = EPOLLIN;
    ep.data.fd = timer_fd;
    if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, timer_fd, &ep) == -1) {
        fprintf(stderr, "Epoll timer error!\n");
        _exit(1);
    }
    //буфер для сообщения
    char buffer[MAX_UDP_SIZE];
    memset(buffer, 0, MAX_UDP_SIZE);
    //одновременно может прийти и дейтаграмма и сработать таймер, поэтому нужен массив текущих событий epoll
    struct epoll_event events[MAX_EPOLL_EVENTS];
    
    //устанавливем первым для запуска 0-й тред
    int curr_thread = 0;
    //длина предыдущего сообщения
    int buffer_len;
    
    //поднимаем семафор - разрешаем работу с сообщениями тредам
    struct sembuf sem;
    sem.sem_num = 0;
    sem.sem_op = 1;
    sem.sem_flg = 0;
    //если тредов нет, то никаких семафоров не используем
    if (thread_num != 0) {
        if (semop(smid, &sem, 1) == -1) {
            fprintf(stderr, "Semop error!\n");
            _exit(1);
        }
    }
    //основной цикл обработки сообщений
    while(1) {
        //ждем события в неблокирующем режиме
        int num_of_events = epoll_wait(ep_fd, events, MAX_EPOLL_EVENTS, 0);
        //здесь пришло 1 или 2 события, обрабатываем их
        for (int i = 0; i < num_of_events; i++) {
            //если данное событие - обработка сообщения на сокете
            if (events[i].data.fd == udp_socket) {
                //считем длину старого сообщения
                buffer_len = strlen(buffer);
                //и зачищаем столько байт, сколько оно занимало, чтобы не было неприятностей
                memset(buffer, 0, buffer_len);
                //получаем новое сообщение
                recv(udp_socket, &buffer, MAX_UDP_SIZE, 0);
                //поскольку после этого момента почти моментально может начать работу другой тред
                //копируем сообщение и передаем уже копию, а не указатель на то место, куда мы считывали сообщение
                char arg[MAX_UDP_SIZE];
                strcpy(arg, buffer);
                //если треды есть, то по текущему номеру создаем тред, запущенный с функцией обработки
                if (thread_num != 0) {
                    if (pthread_create(&threads[curr_thread], NULL, function, (void*)arg) == -1) {
                        fprintf(stderr, "Thread error!\n");
                        _exit(1);
                    }
                    curr_thread++;
                    //если все треды заполнились, то ждем завершения их всех
                    //учитывая то что тредов может быть достаточно много, то к этому моменту все они завершается
                    //и этот цикл не будет долго стоять в ожидании завершения какого-то треда
                    if (curr_thread == thread_num) {
                        for (int i = 0; i < thread_num; i++) {
                            pthread_join(threads[i], NULL);
                        }
                        curr_thread = 0;
                    }
                //если треды не используем - просто запускаем функцию обработки сообщения
                } else {
                    function((void*)buffer);
                }
            }
            //если собтытие - истечение таймаута (epoll получил доступ к чтению из таймера)
            if (events[i].data.fd == timer_fd) {
                //ждем завершения всех работющих тредов
                if (thread_num != 0) {
                    for (int i = 0; i < curr_thread; i++) {
                        pthread_join(threads[i], NULL);
                    }
                    curr_thread = 0;
                }
                //обнуляем таймер
                set_timer(timer_fd, timeout_sec);
                //выводим статистику
                printf("-----------------------------------------\n");
                printf("total requests: %lld\n", requests);
                printf("total requests for sorting: %lld\n", requests_s);
                printf("total requests for getting sum: %lld\n", requests_p);
                printf("total sendings: %lld\n", sendings);
                printf("total sendings for sorting: %lld\n", sendings_s);
                printf("total sendings for getting sum: %lld\n", sendings_p);
                printf("-----------------------------------------\n");
                //обнуляем статистику
                requests = 0;
                requests_s = 0;
                requests_p = 0;
                sendings = 0;
                sendings_s = 0;
                sendings_p = 0;
            }
            events[i].data.fd = 0;
        }
    }
    close(timer_fd);
    close(ep_fd);
    close(udp_socket);
    shmctl(smid, IPC_RMID, 0);
    return 0;
}

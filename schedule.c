#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/param.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#define MAX_THREADS 10

void *executeProcess(void *ptr);
void *generateBursts(void *ptr);
void *generateBurstsFromFile(void *ptr);
void *addToQueue(int threadIndex, int burstIndex, double burstLength, long wallClockTime);
void *printQueue();
void *executeByFCFS();
void *executeBySJF(int threadCount, int totalBurstCount);
void *executeByPrio(int threadCount, int totalBurstCount);
static void *executeByVruntime(int threadCount, int totalBurstCount);

pthread_mutex_t mutex;
pthread_cond_t cond;



struct jobInfo {
    int N;
    int Bcount;
    int minB;
    int avgB;
    int minA;
    int avgA;
    int threadIndex;
    char* inprefix;
};

struct SThreadInfo {
    int N;
    int Bcount;
    char *ALG;
    int totalBCount;
};

struct Node {
    struct burstInfo* burst;
    struct Node* next;
};
struct Node* rq = NULL;

struct burstInfo {
    int threadIndex;
    int burstIndex;
    double length; // ms
    long int wallClockTime; // time the burst is generated
};

int main(int argc, char **argv) {
    int N;
    int Bcount;
    int minB;
    int avgB;
    int minA;
    int avgA;
    char* ALG;
    if (argc == 8) {
        N = atoi(argv[1]);
        Bcount = atoi(argv[2]);
        minB = atoi(argv[3]);
        avgB = atoi(argv[4]);
        minA = atoi(argv[5]);
        avgA = atoi(argv[6]);
        ALG = argv[7];
    } else if (argc == 5) {
        N = atoi(argv[1]);
        ALG = argv[2];
    }

    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    pthread_t wThreads[N]; // W threads
    struct jobInfo info[N];

    pthread_t sThread; // S thread
    struct SThreadInfo infoS;
    if (argc == 8) {
        infoS.Bcount = Bcount;
        infoS.N = N;
        infoS.ALG = ALG;
        infoS.totalBCount = -1;
    } else {
        char* inprefix = argv[4];
        char dash = '-';
        char *extension = ".txt";
        strncat(inprefix, &dash, 1);
        FILE* fp;
        char* line = NULL;
        size_t len = 0;
        ssize_t read;
        char threadNum[10];
        char fileName[20];
        int burstCount = 0;
        for (int i = 0; i < N; i++) {
            strcpy(fileName, inprefix);
            sprintf(threadNum, "%d", i+1);
            strncat(fileName, threadNum, strlen(threadNum));
            strncat(fileName, extension, strlen(extension));
            fp = fopen(fileName, "r");
            while ((read = getline(&line, &len, fp)) != -1) {
                if (line[0] == '\n') {
                    continue;
                }
                burstCount++;
            }
            fclose(fp);
        }
        infoS.totalBCount = burstCount;
        infoS.ALG = ALG;
        infoS.Bcount = -1;
        infoS.N = N;
    }
    pthread_create(&sThread, NULL, executeProcess, &infoS);

    // Create Threads
    if (argc == 8) {
        for (int i = 0; i < N; i++) {
            info[i].N = N;
            info[i].Bcount = Bcount;
            info[i].minB = minB;
            info[i].avgB = avgB;
            info[i].minA = minA;
            info[i].avgA = avgA;
            info[i].threadIndex = i + 1;
            pthread_create(&wThreads[i], NULL, generateBursts, &info[i]);
        }
    } else {
        for (int i = 0; i < N; i++) {
            info[i].N = N;
            info[i].threadIndex = i + 1;
            info[i].inprefix = argv[4];
            pthread_create(&wThreads[i], NULL, generateBurstsFromFile, &info[i]);
        }
    }
    
    
    // Wait for threads
    for (int i = 0; i < N; i++) {
        pthread_join(wThreads[i], NULL);
    }
    pthread_join(sThread, NULL);

    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    return 0;
}

void *generateBurstsFromFile(void *ptr) {
    struct jobInfo* info = (struct jobInfo*) ptr;
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);
    srand(currentTime.tv_usec + info->threadIndex);

    // Get file name
    char *inprefix = malloc(strlen(info->inprefix) + 1); 
    strcpy(inprefix, info->inprefix);
    char threadNum[10];
    sprintf(threadNum, "%d", info->threadIndex);
    strncat(inprefix, threadNum, strlen(threadNum));
    char *extension = ".txt";
    strncat(inprefix, extension, strlen(extension));
    FILE* fp = fopen(inprefix, "r");
    char* line = NULL;
    size_t len = 0;
    ssize_t read;
    int lineCount = 0;
    int i = 1;
    while ((read = getline(&line, &len, fp)) != -1) {
        if (line[0] == '\n') {
            continue;
        }
        char *interarrivaltime = strtok(line, " ");
        char *bursttime = strtok(NULL, " ");
        int iat = atoi(interarrivaltime);
        int bt = atoi(bursttime);
        usleep(iat * 1000);
        gettimeofday(&currentTime, NULL);
        long wallClockTime = (currentTime.tv_sec * 1000) + (currentTime.tv_usec / 1000); 
        pthread_mutex_lock(&mutex);
        addToQueue(info->threadIndex, i, bt, wallClockTime);
        pthread_mutex_unlock(&mutex);
        i++;   
    }
    fclose(fp);
    free(inprefix);
    pthread_exit(0);
}

void *executeProcess(void *ptr) {
    struct SThreadInfo* info = (struct SThreadInfo*) ptr;
    int total = 0;
    if (info->totalBCount != -1) {
        total = info->totalBCount;
    } else {
        total = info->Bcount * info->N;
    }
    for (int i = 0; i < total; i++) {
        pthread_mutex_lock(&mutex);
        while (rq == NULL) {
            pthread_cond_wait(&cond, &mutex);
        }
        if (!strcmp(info->ALG, "FCFS")) {
            executeByFCFS();
        } else if (!strcmp(info->ALG, "SJF")) {
            executeBySJF(info->N, total);
        } else if (!strcmp(info->ALG, "PRIO")) {
            executeByPrio(info->N, total);
        } else if (!strcmp(info->ALG, "VRUNTIME")) {
            executeByVruntime(info->N, total);
        }
        pthread_mutex_unlock(&mutex);
    }
    pthread_exit(0); 
};

void getPerformanceMetric(struct Node* nodeToDelete) {
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);
    long wallClockTime = (currentTime.tv_sec * 1000) + (currentTime.tv_usec / 1000); 
    printf("%ld\n",wallClockTime - nodeToDelete->burst->wallClockTime);
}   

void *executeByFCFS() {
    usleep(rq->burst->length * 1000);
    struct Node* nodeToDelete = rq;
    rq = rq->next;
    // getPerformanceMetric(nodeToDelete);
    free(nodeToDelete);
}

void *executeBySJF(int threadCount, int totalBurstCount) {
    int check[threadCount];
    for (int i = 0; i < threadCount; i++) check[i] = 0;
    struct burstInfo* candidates[totalBurstCount];
    for (int i = 0; i < totalBurstCount; i++) candidates[i] = NULL;
    struct Node* curr = rq;
    int counter = 0;
    while (curr != NULL) {
        if (check[curr->burst->threadIndex - 1] == 0) {
            check[curr->burst->threadIndex - 1] = 1;
            candidates[counter] = curr->burst;
            counter++;
        }
        curr = curr->next;
    }

    counter = 1;
    struct burstInfo* min = candidates[0];
    while (candidates[counter] != NULL) {
        if (candidates[counter]->length < min->length) {
            min = candidates[counter];
        }
        counter++;
    }
    usleep(min->length * 1000);
    struct Node *temp = rq, *prev;
    if (temp != NULL && temp->burst->burstIndex == min->burstIndex && temp->burst->threadIndex == min->threadIndex) {
        rq = temp->next;
        // getPerformanceMetric(temp);

        free(temp);
        return 0;
    }
    while (temp != NULL && !(temp->burst->burstIndex == min->burstIndex && temp->burst->threadIndex == min->threadIndex)) {
        prev = temp;
        temp = temp->next;
    }
    prev->next = temp->next;
        // getPerformanceMetric(temp);

    free(temp);
}

void *executeByPrio(int threadCount, int totalBurstCount) {
    int check[threadCount];
    for (int i = 0; i < threadCount; i++) check[i] = 0;
    struct burstInfo* candidates[totalBurstCount];
    for (int i = 0; i < totalBurstCount; i++) candidates[i] = NULL;
    struct Node* curr = rq;
    int counter = 0;
    while (curr != NULL) {
        if (check[curr->burst->threadIndex - 1] == 0) {
            check[curr->burst->threadIndex - 1] = 1;
            candidates[counter] = curr->burst;
            counter++;
        }
        curr = curr->next;
    }
    counter = 1;
    struct burstInfo* highestPriority = candidates[0];
    while (candidates[counter] != NULL) {
        if (candidates[counter]->threadIndex < highestPriority->threadIndex) {
            highestPriority = candidates[counter];
        }
        counter++;
    }

    usleep(highestPriority->length * 1000);
    struct Node *temp = rq, *prev;
    if (temp != NULL && temp->burst->burstIndex == highestPriority->burstIndex && temp->burst->threadIndex == highestPriority->threadIndex) {
        rq = temp->next; 
        // getPerformanceMetric(temp);
        free(temp);

        return 0;
    }
    while (temp != NULL && !(temp->burst->burstIndex == highestPriority->burstIndex && temp->burst->threadIndex == highestPriority->threadIndex)) {
        prev = temp;
        temp = temp->next;
    }
    prev->next = temp->next;
        // getPerformanceMetric(temp);

    free(temp);
}

static void *executeByVruntime(int threadCount, int totalBurstCount) {
    static double vruntime[MAX_THREADS + 1];

    // Find candidate processesfor execution
    int check[threadCount];
    for (int i = 0; i < threadCount; i++) check[i] = 0;
    struct burstInfo* candidates[totalBurstCount];
    for (int i = 0; i < totalBurstCount; i++) candidates[i] = NULL;
    struct Node* curr = rq;
    int counter = 0;
    while (curr != NULL) {
        if (check[curr->burst->threadIndex - 1] == 0) {
            check[curr->burst->threadIndex - 1] = 1;
            candidates[counter] = curr->burst;
            counter++;
        }
        curr = curr->next;
    }
    int threadTurn = candidates[0]->threadIndex;
    int j = 0;
    for (int i = 1; i < counter; i++) {
        if (vruntime[candidates[i]->threadIndex] < vruntime[threadTurn]) {
            threadTurn = candidates[i]->threadIndex;
            j = i;
        }
    }
    struct burstInfo* burstToExecute = candidates[j];
    // Execution and removal logic
    usleep(burstToExecute->length * 1000);
    vruntime[burstToExecute->threadIndex] += burstToExecute->length * (0.7 + (0.3 * (burstToExecute->threadIndex)));
    struct Node *temp = rq, *prev;
    if (temp != NULL && temp->burst->burstIndex == burstToExecute->burstIndex && temp->burst->threadIndex == burstToExecute->threadIndex) {
        rq = temp->next;
        // getPerformanceMetric(temp);
        free(temp);
        return 0;
    }
    while (temp != NULL && !(temp->burst->burstIndex == burstToExecute->burstIndex && temp->burst->threadIndex == burstToExecute->threadIndex)) {
        prev = temp;
        temp = temp->next;
    }
    prev->next = temp->next;
    // getPerformanceMetric(temp);
    free(temp);
}

void *generateBursts(void *ptr) {
    struct jobInfo* info = (struct jobInfo*) ptr;
    double randomNumber;
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);
    srand(currentTime.tv_usec + info->threadIndex);
    for (int j = 0 ; j < info->Bcount; j++) {
        // Sleep 
        double sleepDuration;
        do {
            randomNumber = (double) rand() / (double) RAND_MAX;
            if (randomNumber == 0 || randomNumber == 1) continue;
            sleepDuration = (double) (-info->avgA * log(randomNumber)) * 1000;
        } while (sleepDuration < info->minA);
        usleep(sleepDuration);
        // Burst 
        double burstLength;
        do {
            randomNumber = (double) rand() / (double) RAND_MAX;
            if (randomNumber == 0 || randomNumber == 1) continue;
            burstLength = ((double) -info->avgB) * log(randomNumber);
        } while (burstLength < info->minB);
        gettimeofday(&currentTime, NULL);
        long wallClockTime = (currentTime.tv_sec * 1000) + (currentTime.tv_usec / 1000); 
        pthread_mutex_lock(&mutex);
        addToQueue(info->threadIndex, j+1, burstLength, wallClockTime);
        pthread_mutex_unlock(&mutex);
    }
    pthread_exit(0);
};

void *addToQueue(int threadIndex, int burstIndex, double burstLength, long wallClockTime) {
    if (rq == NULL) {
        rq = (struct Node*) malloc(sizeof(struct Node));
        rq->burst = (struct burstInfo*) malloc(sizeof(struct burstInfo));
        rq->burst->burstIndex = burstIndex;
        rq->burst->length = burstLength;
        rq->burst->threadIndex = threadIndex;
        rq->burst->wallClockTime = wallClockTime;
        rq->next = NULL;
        pthread_cond_signal(&cond);
    } else {
        struct Node* curr = rq;
        while (curr->next != NULL) {
            curr = curr->next;
        }
        curr->next = (struct Node*) malloc(sizeof(struct Node));
        curr = curr->next;
        curr->burst = (struct burstInfo*) malloc(sizeof(struct burstInfo));
        curr->burst->burstIndex = burstIndex;
        curr->burst->length = burstLength;
        curr->burst->threadIndex = threadIndex;
        curr->burst->wallClockTime = wallClockTime;
        curr->next = NULL;
    }   
}

void *printQueue() {
    struct Node* curr = rq;
    while (curr != NULL) {
        printf("%d %d %lf %ld \n", curr->burst->threadIndex, curr->burst->burstIndex, curr->burst->length, curr->burst->wallClockTime);
        curr = curr->next;
    }
}

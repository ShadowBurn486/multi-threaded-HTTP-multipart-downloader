#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

#include "http.h"
#include "queue.h"

#define BUF_SIZE 1024
#define FILE_SIZE 256

typedef struct {
    char *url;
    int min_range;
    int max_range;
    Buffer *result;
}  Task;


typedef struct {
    Queue *todo;
    Queue *done;

    pthread_t *threads;
    int num_workers;

} Context;

void create_directory(const char *dir) {
    struct stat st = { 0 };

    if (stat(dir, &st) == -1) {
        int rc = mkdir(dir, 0700);
        if (rc == -1) {
            perror("mkdir");
            exit(EXIT_FAILURE);
        }
    }
}


void *worker_thread(void *arg) {
    Context *context = (Context *)arg;

    Task *task = (Task *)queue_get(context->todo);
    char *range = (char *)malloc(1024 * sizeof(char));

    while (task) {
        snprintf(range, 1024 * sizeof(char), "%d-%d", task->min_range,
        task->max_range);

        task->result = http_url(task->url, range);

        queue_put(context->done, task);
        task = (Task *)queue_get(context->todo);
    }

    free(range);
    return NULL;
}


Context *spawn_workers(int num_workers) {
    Context *context = (Context*)malloc(sizeof(Context));

    context->todo = queue_alloc(num_workers * 2);
    context->done = queue_alloc(num_workers * 2);

    context->num_workers = num_workers;

    context->threads = (pthread_t*)malloc(sizeof(pthread_t) * num_workers);
    int i = 0;

    for (i = 0; i < num_workers; ++i) {
        if (pthread_create(&context->threads[i], NULL, worker_thread, context) != 0) {
            perror("pthread_create");
            exit(1);
        }
    }

    return context;
}

void free_workers(Context *context) {
    int num_workers = context->num_workers;
    int i = 0;

    for (i = 0; i < num_workers; ++i) {
        queue_put(context->todo, NULL);
    }

    for (i = 0; i < num_workers; ++i) {
        if (pthread_join(context->threads[i], NULL) != 0) {
            perror("pthread_join");
            exit(1);
        }
    }

    queue_free(context->todo);
    queue_free(context->done);

    free(context->threads);
    free(context);
}


Task *new_task(char *url, int min_range, int max_range) {
    Task *task = malloc(sizeof(Task));
    task->result = NULL;
    task->url = malloc(strlen(url) + 1);
    task->min_range = min_range;
    task->max_range = max_range;

    strcpy(task->url, url);

    return task;
}

void free_task(Task *task) {

    if (task->result) {
        free(task->result->data);
        free(task->result);
    }

    free(task->url);
    free(task);
}


void wait_task(const char *download_dir, Context *context) {
    char filename[FILE_SIZE], url_file[FILE_SIZE];
    Task *task = (Task*)queue_get(context->done);

    if (task->result) {

        snprintf(url_file, FILE_SIZE * sizeof(char), "%d", task->min_range);
        size_t len = strlen(url_file);
        for (int i = 0; i < len; ++i) {
            if (url_file[i] == '/') {
                url_file[i] = '|';
            }
        }

        snprintf(filename, FILE_SIZE, "%s/%s", download_dir, url_file);
        FILE *fp = fopen(filename, "w");

        if (fp == NULL) {
            fprintf(stderr, "error writing to: %s\n", filename);
            exit(EXIT_FAILURE);
        }

        char *data = http_get_content(task->result);
        if (data) {
            size_t length = task->result->length - (data - task->result->data);

            fwrite(data, 1, length, fp);
            fclose(fp);

           // printf("downloaded %d bytes from %s\n", (int)length, task->url);
        }
        else {
            printf("error in response from %s\n", task->url);
        }
    }
    else {

        fprintf(stderr, "error downloading: %s\n", task->url);
    }

    free_task(task);
}


/**
 * Merge all files in from src to file with name dest synchronously
 * by reading each file, and writing its contents to the dest file.
 * @param src - char pointer to src directory holding files to merge
 * @param dest - char pointer to name of file resulting from merge
 * @param bytes - The maximum byte size downloaded
 * @param tasks - The tasks needed for the multipart download
 */
void merge_files(char *src, char *dest, int bytes, int tasks) {
    int i;
    for (i = 0; i < strlen(dest); ++i ){
        if(dest[i] == '/'){dest[i] = '+';}      //  Replece "/" for naming a file
    }

    char location[FILE_SIZE];
    sprintf(location, "%s/%s", src, dest);      // File Directory with Name
    int merged_file = open(location, O_CREAT|O_WRONLY|O_APPEND, 0777);  // Create a file for merging all chunk files
    if (merged_file == -1)
    {
        perror("Merged File Error");    // File check
        exit(1);
    }
    char chunk_names[FILE_SIZE];            // Save chunk files names
    char buffer[BUF_SIZE];

    for ( i = 0; i < tasks; ++i) {
        sprintf(chunk_names, "%s/%d", src, i*bytes);    // located the specific chunk file
        int chunk_file = open(chunk_names, O_RDONLY);   //  Open Chunk File
        if (chunk_file == -1)
        {
        perror("Chunk File Error");     // File check
        exit(1);
        }

        while (1) {             // looping for combining all chunk files
            int num_bytes = read(chunk_file, buffer, BUF_SIZE);     // Read Data from chunk file
            if (num_bytes == 0) break;                              // Break loop untill no more data
            write(merged_file, buffer, num_bytes);                  // Save Data to merged file
        }
        close(chunk_file);      // Close file
    }

    close(merged_file);     // Close file
}


/**
 * Remove files caused by chunk downloading
 * @param dir - The directory holding the chunked files
 * @param bytes - The maximum byte size per file. Assumed to be filename
 * @param files - The number of chunked files to remove.
 */
void remove_chunk_files(char *dir, int bytes, int files) {
    char chunk_name[FILE_SIZE];         // Each Chunk Name
    int i = 0;                          // Start art 0
    while(1) {
        sprintf(chunk_name, "%s/%d", dir, i*bytes);     // Locate each chunk file
        if (remove(chunk_name) != 0) break;     // Break Loop, until delete all chunk files
        ++i;
    }
}


int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: ./downloader url_file num_workers download_dir\n");
        exit(1);
    }
    // urls file, number of workers , download location
    char *url_file = argv[1];
    int num_workers = atoi(argv[2]);
    char *download_dir = argv[3];

    create_directory(download_dir);
    FILE *fp = fopen(url_file, "r");    // File descriptor for url_file
    char *line = NULL;                  // Char pointer to locate the URL
    size_t len = 0;                     // Length of URL

    if (fp == NULL) {
        exit(EXIT_FAILURE);
    }

    // spawn threads and create work queue(s)
    Context *context = spawn_workers(num_workers);

    //
    int work = 0, bytes = 0, num_tasks = 0;

    // looping for get each URL from fp (the file where urls located),
    // and &line is to save specific URL, and using for later functions.
    // The != -1 is to make sure there is a URL will get.
    while ((len = getline(&line, &len, fp)) != -1) {

        // Checking "\n" for a line of URL
        if (line[len - 1] == '\n') {
            // And using null byte to replace "\n"
            line[len - 1] = '\0';
        }

        // Get number of tasks, which is the times of a flie should download
        num_tasks = get_num_tasks(line, num_workers);
        if (num_tasks == -1){
            perror("Number of Task Error"); // Get Task error check
            exit(1);
        }
        // The maxmium chunk size for each task
        bytes = get_max_chunk_size();

        // Loop multiply times until all
        for (int i  = 0; i < num_tasks; i ++) {
            // Record all available tasks
            ++work;
            // Put the task to TODO QUEUE for downloading
            // Clarify range for mutiply tasks:
            // i * bytes - (i+1) * bytes - 1
            // 0 - 99
            // 100 - 199
            // 200 - 299 .....  to aviod overlap bytes.
            queue_put(context->todo, new_task(line, i * bytes, (i+1) * bytes - 1));
        }

        // Get results back
        while (work > 0) {
            // Decrease the tasks, and be able to let new task to add in.
            --work;
            // Download the context form task which is currently in TODO QUEUE
            wait_task(download_dir, context);
        }

        // Merge the files -- simple synchronous method
        merge_files(download_dir, line, bytes, num_tasks);
        // Then remove the chunked download files
        remove_chunk_files(download_dir, bytes, num_tasks);
    }

    // Clean up
    fclose(fp);  // Close file descriptor
    free(line);  // Free allocated memory
    free_workers(context);
    return 0;
}

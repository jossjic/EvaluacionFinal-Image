#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <omp.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "filtros_img.h"
#include <mpi.h>

#define MAX_IMAGENES 600

int ends_with_bmp(const char* filename) {
    const char* ext = strrchr(filename, '.');
    return ext && strcmp(ext, ".bmp") == 0;
}

int main(int argc, char** argv) {
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    const char* carpeta = "img";
    struct dirent* entry;
    DIR* dir = opendir(carpeta);

    if (!dir) {
        if (rank == 0)
            perror("No se pudo abrir el directorio 'img'");
        MPI_Finalize();
        return 1;
    }

    int kernel_size;
    if (rank == 0) {
        if (argc < 2) {
            printf("Error: se requiere el tamaño del kernel como argumento.\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
            return 1;
        }

        kernel_size = atoi(argv[1]);
        if (kernel_size < 55 || kernel_size > 155 || kernel_size % 2 == 0) {
            printf("Tamaño de kernel inválido. Se usará 105 por defecto.\n");
            kernel_size = 105;
        }

        printf("Kernel size recibido desde GUI: %d\n", kernel_size);
    }

    MPI_Bcast(&kernel_size, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (rank == 0) printf("Procesando imágenes...\n");

    char* imagenes[MAX_IMAGENES];
    int total = 0;

    while ((entry = readdir(dir)) && total < MAX_IMAGENES) {
        char ruta_completa[256];
        snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s", carpeta, entry->d_name);
        struct stat st;
        if (stat(ruta_completa, &st) == 0 && S_ISREG(st.st_mode) && ends_with_bmp(entry->d_name)) {
            imagenes[total] = malloc(256);
            strcpy(imagenes[total], ruta_completa);
            total++;
        }
    }
    closedir(dir);

    double inicio_local = omp_get_wtime();

    #pragma omp parallel
    {
        #pragma omp for
        for (int i = rank; i < total; i += size) {
            const char* path = imagenes[i];
            const char* nombre = strrchr(path, '/');
            char base[64];
            if (!nombre) nombre = path; else nombre++;
            strncpy(base, nombre, strchr(nombre, '.') - nombre);
            base[strchr(nombre, '.') - nombre] = '\0';

            #pragma omp critical
            {
                printf("Proceso %d procesando imagen: %s\n", rank, base);
                fflush(stdout);
            }

            char out1[100], out2[100], out3[100], out4[100], out5[100], out6[100];
            sprintf(out1, "/home/mpiu/destinoBash/%s_gray.bmp", base);
            sprintf(out2, "/home/mpiu/destinoBash/%s_hinv_color.bmp", base);
            sprintf(out3, "/home/mpiu/destinoBash/%s_vinv_color.bmp", base);
            sprintf(out4, "/home/mpiu/destinoBash/%s_hinv_gray.bmp", base);
            sprintf(out5, "/home/mpiu/destinoBash/%s_vinv_gray.bmp", base);
            sprintf(out6, "/home/mpiu/destinoBash/%s_blur_%d.bmp", base, kernel_size);

            to_grayscale(path, out1, base);
            #pragma omp critical
            { printf("-> [%d] Filtro gris aplicado: %s\n", rank, out1); fflush(stdout); }

            mirror_horizontal_color(path, out2, base);
            #pragma omp critical
            { printf("-> [%d] Espejo horizontal color: %s\n", rank, out2); fflush(stdout); }

            mirror_vertical_color(path, out3, base);
            #pragma omp critical
            { printf("-> [%d] Espejo vertical color: %s\n", rank, out3); fflush(stdout); }

            mirror_horizontal_gray(path, out4, base);
            #pragma omp critical
            { printf("-> [%d] Espejo horizontal gris: %s\n", rank, out4); fflush(stdout); }

            mirror_vertical_gray(path, out5, base);
            #pragma omp critical
            { printf("-> [%d] Espejo vertical gris: %s\n", rank, out5); fflush(stdout); }

            apply_blur(path, out6, base, kernel_size);
            #pragma omp critical
            { printf("-> [%d] Blur: %s\n", rank, out6); fflush(stdout); }

            free(imagenes[i]);
        }
    }

    double fin_local = omp_get_wtime();
    double tiempo_local = fin_local - inicio_local;
    MPI_Barrier(MPI_COMM_WORLD);
    double suma_tiempos = 0;
    MPI_Reduce(&tiempo_local, &suma_tiempos, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("\nTiempo total acumulado de todos los procesos: %.4f segundos\n", suma_tiempos);

        long long total_lecturas = 0;
        long long total_escrituras = 0;
        DIR* logdir = opendir("./logs");
        struct dirent* logentry;

        if (logdir) {
            while ((logentry = readdir(logdir))) {
                if (strstr(logentry->d_name, ".txt")) {
                    char logpath[256];
                    snprintf(logpath, sizeof(logpath), "./logs/%s", logentry->d_name);
                    FILE* logf = fopen(logpath, "r");
                    if (logf) {
                        char buffer[128];
                        while (fgets(buffer, sizeof(buffer), logf)) {
                            long long lecturas = 0, escrituras = 0;
                            if (sscanf(buffer, "Lecturas: %lld", &lecturas) == 1)
                                total_lecturas += lecturas;
                            if (sscanf(buffer, "Escrituras: %lld", &escrituras) == 1)
                                total_escrituras += escrituras;
                        }
                        fclose(logf);
                    }
                }
            }
            closedir(logdir);
        }

        long long instrucciones_totales = (total_lecturas + total_escrituras) * 20LL;
        double tiempo_total = suma_tiempos;
        if (tiempo_total <= 0) tiempo_total = 1e-6;

        double mips_global = instrucciones_totales / (tiempo_total * 1e6);
        long long bytes_totales = total_lecturas + total_escrituras;
        double bytes_por_segundo_global = bytes_totales / tiempo_total;

        FILE* resumen = fopen("./reporte_total.txt", "w");
        if (resumen) {
            fprintf(resumen, "Lecturas totales: %lld\n", total_lecturas);
            fprintf(resumen, "Escrituras totales: %lld\n", total_escrituras);
            fprintf(resumen, "Instrucciones totales: %lld\n", instrucciones_totales);
            fprintf(resumen, "Tiempo total: %lf segundos\n", tiempo_total);
            fprintf(resumen, "MIPS global: %lf\n", mips_global);
            fprintf(resumen, "Bytes por segundo global: %lf\n", bytes_por_segundo_global);
            fclose(resumen);
        }

        printf("Reporte generado correctamente por el proceso 0.\n");
    }

    MPI_Finalize();
    return 0;
}
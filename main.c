#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <omp.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "filtros_img.h"
#include <unistd.h>
#include <mpi.h>

#define MAX_IMAGENES 600

void configure_threads_by_host() {
    char hostname[1024];
    gethostname(hostname, sizeof(hostname));

    if (strstr(hostname, "master")) {
        omp_set_num_threads(20);
    } else if (strstr(hostname, "luis-QEMU-Virtual-Machine")) {
        omp_set_num_threads(4);
    } else if (strstr(hostname, "slave2")) {
        omp_set_num_threads(4);
    } else {
        omp_set_num_threads(2); 

    #pragma omp parallel
    {
        #pragma omp single
        printf("Host %s usando %d hilos (OpenMP)\n", hostname, omp_get_num_threads());
    }
}

void compare_execution_costs(double total_exec_time_seconds) {
    double total_kwh_week = 13.559;

    // Energy price in USD per kWh (with subsidy)
    double cost_kwh_usd = 0.013;

    // Annual maintenance cost (USD)
    double annual_maintenance_usd = 78.17 + 62.53 + 20.84;

    // Equipment cost (MXN)
    double equipment_cost_mxn = 37429 + 11239 + 9709;
    double exchange_rate = 19.0;
    double equipment_cost_usd = equipment_cost_mxn / exchange_rate;

    // AWS annual cost (fixed)
    double aws_annual_cost_usd = 3047.16;

    // Weekly execution time in seconds (8h/day * 5 days)
    double seconds_per_week = 40.0 * 60.0 * 60.0;

    // Energy consumed proportionally to execution time
    double kwh_used = (total_exec_time_seconds / seconds_per_week) * total_kwh_week;
    double local_energy_cost = kwh_used * cost_kwh_usd;

    // Maintenance cost proportional to execution time
    double proportional_maintenance = (annual_maintenance_usd / (365 * 24 * 3600)) * total_exec_time_seconds;
    double local_total_cost = local_energy_cost + proportional_maintenance;
    double annual_local_cost = local_total_cost * 16 * 5 * 4 * 12;
    printf("\n--- Comparación de costos de ejecución ---\n");
    printf("• Tiempo total de ejecución: %.2f segundos\n", total_exec_time_seconds);
    printf("• Energía consumida localmente: %.6f kWh\n", kwh_used);
    printf("• Costo energético local: $%.4f USD\n", local_energy_cost);
    printf("• Costo de mantenimiento proporcional: $%.6f USD\n", proportional_maintenance);
    printf("• Costo total de ejecución local: $%.6f USD\n", local_total_cost);
    printf("• Costo anual del servicio AWS: $%.2f USD\n", aws_annual_cost_usd);
    printf("• Costo anual local: $%.2f USD\n", annual_local_cost);

    // Append to report
    FILE* cost_file = fopen("./reporte_total.txt", "a");
    if (cost_file) {
        fprintf(cost_file, "\n=== Comparación de costos ===\n");
        fprintf(cost_file, "Tiempo de ejecución (s): %.2f\n", total_exec_time_seconds);
        fprintf(cost_file, "Consumo energético local (kWh): %.6f\n", kwh_used);
        fprintf(cost_file, "Costo energético local (USD): %.4f\n", local_energy_cost);
        fprintf(cost_file, "Costo de mantenimiento proporcional (USD): %.6f\n", proportional_maintenance);
        fprintf(cost_file, "Costo total local (USD): %.6f\n", local_total_cost);
        fprintf(cost_file, "Costo anual AWS (USD): %.2f\n", aws_annual_cost_usd);
        fclose(cost_file);
    }
}


int ends_with_bmp(const char* filename) {
    const char* ext = strrchr(filename, '.');
    return ext && strcmp(ext, ".bmp") == 0;
}

int main(int argc, char** argv) {
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    configure_threads_by_host();

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
        #pragma omp single
        printf("Proceso %d usando %d hilos OpenMP\n", rank, omp_get_num_threads());

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

    // Time of all processes
    double tiempo_maximo = 0;
    MPI_Reduce(&tiempo_local, &tiempo_maximo, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("\nTiempo real de ejecución (mayor entre todos los procesos): %.4f segundos\n", tiempo_maximo);

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
        double tiempo_total = tiempo_maximo;
        if (tiempo_total <= 0) tiempo_total = 1e-6;

        double mips_global = instrucciones_totales / (tiempo_total * 1e6);
        long long bytes_totales = total_lecturas + total_escrituras;
        double bytes_por_segundo_global = bytes_totales / tiempo_total;

        FILE* resumen = fopen("./reporte_total.txt", "w");
        if (resumen) {
	    fprintf(resumen, "Instrucciones totales: %lf\n", (double)instrucciones_totales);
	    fprintf(resumen, "Lecturas totales: %lf\n", (double)total_lecturas);
	    fprintf(resumen, "Escrituras totales: %lf\n", (double)total_escrituras);
	    fprintf(resumen, "Tiempo total: %lf segundos\n", tiempo_total);
	    fprintf(resumen, "MIPS global: %.6e\n", mips_global);
	    fprintf(resumen, "Bytes por segundo global: %.6e\n", bytes_por_segundo_global);
            fclose(resumen);
        }

	compare_execution_costs(tiempo_maximo);
        printf("Reporte generado correctamente por el proceso 0.\n");
    }

    MPI_Finalize();
    return 0;
}
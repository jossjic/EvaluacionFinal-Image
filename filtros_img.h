// filtros_img.h
#ifndef FILTROS_IMG_H
#define FILTROS_IMG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

void generar_log(const char* nombre, const char* tipo, long long lecturas, long long escrituras, double tiempo) {
    char ruta[128];
    sprintf(ruta, "./logs/%s_%s.txt", nombre, tipo);    
    FILE* log = fopen(ruta, "w");

    long long instrucciones = (lecturas + escrituras) * 20LL;
    if (tiempo <= 0.000001) {
        tiempo = 0.000001;
    }
    double mips = instrucciones / (tiempo * 1e6);
    long bytes_totales = lecturas + escrituras;
    double bytes_por_segundo = bytes_totales / tiempo;

    fprintf(log, "Archivo: %s.bmp\nTipo: %s\nLecturas: %lld\nEscrituras: %lld\nTiempo: %lf\nMIPS: %lf\nBytes por segundo: %lf\n",
        nombre, tipo, lecturas, escrituras, tiempo, mips, bytes_por_segundo);

    fclose(log);
}

void to_grayscale(const char* in, const char* out, const char* nombre_base) {
    FILE *f = fopen(in, "rb");
    FILE *fo = fopen(out, "wb");

    if (!f || !fo) {
        fprintf(stderr, "Error al abrir archivos.\n");
        return;
    }

    unsigned char header[54];
    fread(header, sizeof(unsigned char), 54, f);
    fwrite(header, sizeof(unsigned char), 54, fo);

    int ancho = *(int*)&header[18];
    int alto = *(int*)&header[22];
    int row_padded = (ancho * 3 + 3) & (~3); // Asegura múltiplo de 4 bytes
    int tam = row_padded * alto;

    unsigned char* data = (unsigned char*) malloc(tam);
    fread(data, tam, 1, f);

    double t0 = omp_get_wtime();
    int lecturas = 0, escrituras = 0;

    for (int i = 0; i < tam; i += row_padded) {
        for (int j = 0; j < ancho * 3; j += 3) {
            unsigned char b = data[i + j];
            unsigned char g = data[i + j + 1];
            unsigned char r = data[i + j + 2];
            unsigned char gray = 0.21*r + 0.72*g + 0.07*b;
            data[i + j] = data[i + j + 1] = data[i + j + 2] = gray;
            lecturas += 3;
            escrituras += 3;
        }
    }

    double t1 = omp_get_wtime();
    fwrite(data, tam, 1, fo);

    generar_log(nombre_base, "grises", lecturas, escrituras, t1 - t0);
    fclose(f);
    fclose(fo);
    free(data);
}

void mirror_horizontal_color(const char* in, const char* out, const char* nombre_base) {
    FILE *f = fopen(in, "rb");
    FILE *fo = fopen(out, "wb");

    if (!f || !fo) {
        fprintf(stderr, "Error al abrir archivos.\n");
        return;
    }

    // Leer encabezado de archivo (14 bytes)
    unsigned char file_header[14];
    fread(file_header, 1, 14, f);

    // Leer tamaño del DIB header (4 bytes)
    int dib_header_size;
    fread(&dib_header_size, 4, 1, f);

    // Leer el resto del DIB header
    unsigned char* dib_header = (unsigned char*) malloc(dib_header_size - 4);
    fread(dib_header, 1, dib_header_size - 4, f);

    // Ensamblar el encabezado completo para escribirlo igual al original
    int header_total = 14 + dib_header_size;
    unsigned char* full_header = (unsigned char*) malloc(header_total);
    memcpy(full_header, file_header, 14);
    memcpy(full_header + 14, &dib_header_size, 4);
    memcpy(full_header + 18, dib_header, dib_header_size - 4);
    fwrite(full_header, 1, header_total, fo);

    // Obtener información crítica
    int offset_pixels = *(int*)&file_header[10];
    int ancho = *(int*)&full_header[18];
    int alto = *(int*)&full_header[22];
    short bpp = *(short*)&full_header[28];
    int compression = *(int*)&full_header[30];

    if (bpp != 24 || compression != 0) {
        fprintf(stderr, "[ERROR] Solo se soportan BMPs de 24 bits sin compresión. bpp=%d, compression=%d\n", bpp, compression);
        fclose(f); fclose(fo);
        free(dib_header); free(full_header);
        return;
    }

    fseek(f, offset_pixels, SEEK_SET);
    fseek(fo, offset_pixels, SEEK_SET);

    int row_padded = (ancho * 3 + 3) & (~3);
    int tam = row_padded * alto;
    unsigned char* data = (unsigned char*) malloc(tam);
    fread(data, tam, 1, f);

    double t0 = omp_get_wtime();
    int lecturas = 0, escrituras = 0;

    for (int i = 0; i < alto; i++) {
        unsigned char* row = data + i * row_padded;
        unsigned char* temp = (unsigned char*) malloc(row_padded);

        // Espejo horizontal por fila
        for (int j = 0; j < ancho; j++) {
            int src = j * 3;
            int dst = (ancho - 1 - j) * 3;
            temp[dst] = row[src];
            temp[dst + 1] = row[src + 1];
            temp[dst + 2] = row[src + 2];
            lecturas += 3;
            escrituras += 3;
        }

        // Copiar padding sin tocar
        memcpy(temp + ancho * 3, row + ancho * 3, row_padded - ancho * 3);
        memcpy(row, temp, row_padded);
        free(temp);
    }

    double t1 = omp_get_wtime();
    fwrite(data, tam, 1, fo);

    generar_log(nombre_base, "espejo_horizontal_color", lecturas, escrituras, t1 - t0);

    fclose(f);
    fclose(fo);
    free(data);
    free(dib_header);
    free(full_header);
}

void mirror_vertical_color(const char* in, const char* out, const char* nombre_base) {
    FILE *f = fopen(in, "rb");
    FILE *fo = fopen(out, "wb");

    if (!f || !fo) {
        fprintf(stderr, "Error al abrir archivos.\n");
        return;
    }

    // Leer encabezado de archivo (14 bytes)
    unsigned char file_header[14];
    fread(file_header, 1, 14, f);

    // Leer tamaño del DIB header
    int dib_header_size;
    fread(&dib_header_size, 4, 1, f);

    // Leer el resto del DIB header
    unsigned char* dib_header = (unsigned char*) malloc(dib_header_size - 4);
    fread(dib_header, 1, dib_header_size - 4, f);

    // Combinar encabezado completo
    int header_total = 14 + dib_header_size;
    unsigned char* full_header = (unsigned char*) malloc(header_total);
    memcpy(full_header, file_header, 14);
    memcpy(full_header + 14, &dib_header_size, 4);
    memcpy(full_header + 18, dib_header, dib_header_size - 4);
    fwrite(full_header, 1, header_total, fo);

    // Obtener metadatos críticos
    int offset_pixels = *(int*)&file_header[10];
    int ancho = *(int*)&full_header[18];
    int alto = *(int*)&full_header[22];
    short bpp = *(short*)&full_header[28];
    int compression = *(int*)&full_header[30];

    if (bpp != 24 || compression != 0) {
        fprintf(stderr, "[ERROR] Solo BMPs de 24 bits sin compresión son compatibles. bpp=%d, compression=%d\n", bpp, compression);
        fclose(f); fclose(fo);
        free(dib_header); free(full_header);
        return;
    }

    fseek(f, offset_pixels, SEEK_SET);
    fseek(fo, offset_pixels, SEEK_SET);

    int row_padded = (ancho * 3 + 3) & (~3);
    int tam = row_padded * alto;

    // Leer toda la imagen en memoria (con padding)
    unsigned char* data = (unsigned char*) malloc(tam);
    fread(data, 1, tam, f);

    double t0 = omp_get_wtime();
    int lecturas = tam;
    int escrituras = tam;

    // Escribir las filas en orden invertido
    for (int y = alto - 1; y >= 0; y--) {
        fwrite(data + y * row_padded, 1, row_padded, fo);
    }

    double t1 = omp_get_wtime();
    generar_log(nombre_base, "espejo_vertical_color", lecturas, escrituras, t1 - t0);

    fclose(f);
    fclose(fo);
    free(data);
    free(dib_header);
    free(full_header);
}

void mirror_horizontal_gray(const char* in, const char* out, const char* nombre_base) {
    char temp_path[100];
    sprintf(temp_path, "temp_%s.bmp", nombre_base);

    to_grayscale(in, temp_path, nombre_base);
    mirror_horizontal_color(temp_path, out, nombre_base);
    remove(temp_path);
}

void mirror_vertical_gray(const char* in, const char* out, const char* nombre_base) {
    char temp_path[100];
    sprintf(temp_path, "temp_%s.bmp", nombre_base);

    to_grayscale(in, temp_path, nombre_base);
    mirror_vertical_color(temp_path, out, nombre_base);
    remove(temp_path);
}

void apply_blur(const char* in, const char* out, const char* nombre_base, int kernel_size) {
    FILE *image, *outputImage;
    image = fopen(in, "rb");
    outputImage = fopen(out, "wb");

    if (!image || !outputImage) {
        printf("Error abriendo archivo.\n");
        return;
    }

    double t0 = omp_get_wtime();

    unsigned char header[54];
    fread(header, sizeof(unsigned char), 54, image);
    fwrite(header, sizeof(unsigned char), 54, outputImage);

    int width = *(int*)&header[18];
    int height = *(int*)&header[22];
    int row_padded = (width * 3 + 3) & (~3);

    unsigned char** input_rows = (unsigned char**)malloc(height * sizeof(unsigned char*));
    unsigned char** output_rows = (unsigned char**)malloc(height * sizeof(unsigned char*));

    int localidades_leidas = 54;
    int localidades_escritas = 54;

    for (int i = 0; i < height; i++) {
        input_rows[i] = (unsigned char*)malloc(row_padded);
        output_rows[i] = (unsigned char*)malloc(row_padded);
        fread(input_rows[i], sizeof(unsigned char), row_padded, image);
        localidades_leidas += row_padded;
    }

    int k = kernel_size / 2;

    // Blur horizontal
    unsigned char** temp_rows = (unsigned char**)malloc(height * sizeof(unsigned char*));
    for (int y = 0; y < height; y++) {
        temp_rows[y] = (unsigned char*)malloc(row_padded);
        for (int x = 0; x < width; x++) {
            int sumB = 0, sumG = 0, sumR = 0, count = 0;

            for (int dx = -k; dx <= k; dx++) {
                int nx = x + dx;
                if (nx >= 0 && nx < width) {
                    int idx = nx * 3;
                    sumB += input_rows[y][idx + 0];
                    sumG += input_rows[y][idx + 1];
                    sumR += input_rows[y][idx + 2];
                    count++;
                }
            }

            int index = x * 3;
            temp_rows[y][index + 0] = sumB / count;
            temp_rows[y][index + 1] = sumG / count;
            temp_rows[y][index + 2] = sumR / count;
        }

        for (int p = width * 3; p < row_padded; p++) {
            temp_rows[y][p] = input_rows[y][p];
        }
    }

    // Blur vertical
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sumB = 0, sumG = 0, sumR = 0, count = 0;

            for (int dy = -k; dy <= k; dy++) {
                int ny = y + dy;
                if (ny >= 0 && ny < height) {
                    int idx = x * 3;
                    sumB += temp_rows[ny][idx + 0];
                    sumG += temp_rows[ny][idx + 1];
                    sumR += temp_rows[ny][idx + 2];
                    count++;
                }
            }

            int index = x * 3;
            output_rows[y][index + 0] = sumB / count;
            output_rows[y][index + 1] = sumG / count;
            output_rows[y][index + 2] = sumR / count;
        }

        for (int p = width * 3; p < row_padded; p++) {
            output_rows[y][p] = temp_rows[y][p];
        }
    }

    for (int i = 0; i < height; i++) {
        fwrite(output_rows[i], sizeof(unsigned char), row_padded, outputImage);
        free(input_rows[i]);
        free(output_rows[i]);
        free(temp_rows[i]);
        localidades_escritas += row_padded;
    }

    double t1 = omp_get_wtime();
    double tiempo_total = t1 - t0;

    generar_log(nombre_base, "blur", localidades_leidas, localidades_escritas, tiempo_total);

    free(input_rows);
    free(output_rows);
    free(temp_rows);
    fclose(image);
    fclose(outputImage);
}

#endif
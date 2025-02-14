// Frame Generation OMG Open-Source Mezosoic Graphics
// Funciona em todas as GPUs e utiliza OpenCV para calcular o Optical Flow

#include <opencv2/opencv.hpp>
#include <cstdint>
#include <cstdlib>
#include <iostream>

// Expondo uma interface em C para facilitar a integração com Hydra.so
extern "C" {

    // Converte uma imagem RGBA para escala de cinza.
    // Cada pixel possui 4 bytes (RGBA). O resultado é um array com 1 byte por pixel.
    void ConvertToGrayscale(uint8_t* colorPixels, uint8_t* grayPixels, uint32_t width, uint32_t height) {
        uint32_t numPixels = width * height;
        for (uint32_t i = 0; i < numPixels; i++) {
            uint8_t r = colorPixels[i * 4 + 0];
            uint8_t g = colorPixels[i * 4 + 1];
            uint8_t b = colorPixels[i * 4 + 2];
            // Fórmula padrão para conversão: Y = 0.299R + 0.587G + 0.114B
            grayPixels[i] = static_cast<uint8_t>(0.299 * r + 0.587 * g + 0.114 * b);
        }
    }

    // Calcula o Optical Flow entre dois frames em tons de cinza usando o método de Farneback.
    // Retorna um cv::Mat do tipo CV_32FC2 contendo os vetores de fluxo.
    cv::Mat ComputeOpticalFlow(uint8_t* lastGray, uint8_t* currentGray, uint32_t width, uint32_t height) {
        cv::Mat prevFrame(height, width, CV_8UC1, lastGray);
        cv::Mat currFrame(height, width, CV_8UC1, currentGray);
        cv::Mat flow(height, width, CV_32FC2);
        // Parâmetros: pyr_scale=0.5, levels=3, winsize=15, iterations=3, poly_n=5, poly_sigma=1.2, flags=0
        cv::calcOpticalFlowFarneback(prevFrame, currFrame, flow, 0.5, 3, 15, 3, 5, 1.2, 0);
        return flow;
    }

    // Gera o frame interpolado usando os Motion Vectors calculados pelo Optical Flow.
    // 'alpha' determina o quanto do fluxo é aplicado (0.0 = último frame, 1.0 = frame atual).
    // 'lastPixels' e 'currentPixels' são os arrays de RGBA dos dois frames consecutivos.
    // 'outputPixels' deve apontar para um buffer com tamanho (width * height * 4) bytes.
    void GenerateInterpolatedFrame(float alpha, uint8_t* lastPixels, uint8_t* currentPixels, 
                                   uint32_t width, uint32_t height, uint8_t* outputPixels) {
        uint32_t numPixels = width * height;

        // Alocar buffers temporários para os frames em escala de cinza
        uint8_t* lastGray = new uint8_t[numPixels];
        uint8_t* currentGray = new uint8_t[numPixels];

        // Converter os frames RGBA para grayscale
        ConvertToGrayscale(lastPixels, lastGray, width, height);
        ConvertToGrayscale(currentPixels, currentGray, width, height);

        // Calcular Optical Flow entre os frames em grayscale
        cv::Mat flow = ComputeOpticalFlow(lastGray, currentGray, width, height);

        // Criar matrizes OpenCV para o frame RGBA do último frame e para a saída
        cv::Mat lastMat(height, width, CV_8UC4, lastPixels);
        cv::Mat interpMat(height, width, CV_8UC4, outputPixels);

        // Criar mapas de remapeamento para o cv::remap
        cv::Mat mapX(height, width, CV_32FC1);
        cv::Mat mapY(height, width, CV_32FC1);

        // Para cada pixel, calcular a nova posição aplicando o fluxo com o fator alpha
        for (int y = 0; y < (int)height; y++) {
            for (int x = 0; x < (int)width; x++) {
                cv::Vec2f flowAtPixel = flow.at<cv::Vec2f>(y, x);
                float newX = x + alpha * flowAtPixel[0];
                float newY = y + alpha * flowAtPixel[1];
                mapX.at<float>(y, x) = newX;
                mapY.at<float>(y, x) = newY;
            }
        }

        // Gerar o frame interpolado usando remapeamento linear
        cv::remap(lastMat, interpMat, mapX, mapY, cv::INTER_LINEAR);

        // Liberar os buffers temporários
        delete[] lastGray;
        delete[] currentGray;
    }

    // Função exportada que será chamada pelo Hydra.so para gerar o frame intermediário "omg".
    // Parâmetros:
    //   alpha: fator de interpolação (0.0 a 1.0).
    //   lastPixels: ponteiro para o frame anterior em RGBA.
    //   currentPixels: ponteiro para o frame atual em RGBA.
    //   width, height: dimensões da imagem.
    //   outputPixels: buffer onde o frame interpolado (RGBA) será armazenado.
    void GenerateOmgFrame(float alpha, uint8_t* lastPixels, uint8_t* currentPixels, 
                          uint32_t width, uint32_t height, uint8_t* outputPixels) {
        GenerateInterpolatedFrame(alpha, lastPixels, currentPixels, width, height, outputPixels);
    }
}



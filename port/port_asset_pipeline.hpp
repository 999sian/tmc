#pragma once

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace PortAssetPipeline {

std::vector<uint8_t> DecodeGbaTiledGfx(const std::vector<uint8_t>& gfxData, uint16_t width, uint16_t height,
                                       uint8_t bpp);
std::vector<uint8_t> EncodeGbaTiledGfx(const std::vector<uint8_t>& pixels, uint16_t width, uint16_t height,
                                       uint8_t bpp);

bool WriteIndexedBmp(const std::filesystem::path& outputPath, const std::vector<uint8_t>& pixels, uint16_t width,
                     uint16_t height, uint8_t bpp, std::string* error = nullptr);
bool ReadEditableBmp(const std::filesystem::path& inputPath, std::vector<uint8_t>& pixels, uint16_t expectedWidth,
                     uint16_t expectedHeight, uint8_t bpp, std::string* error = nullptr);

bool WritePaletteJson(const std::filesystem::path& outputPath, const std::vector<uint8_t>& paletteData,
                      std::string* error = nullptr);
bool ReadPaletteJson(const std::filesystem::path& inputPath, std::vector<uint8_t>& paletteData,
                     std::string* error = nullptr);
bool DecodeTmcText(const uint8_t* textData, size_t maxBytes, std::string& text, size_t* consumedBytes = nullptr,
                   std::string* error = nullptr);
bool EncodeTmcText(const std::string& text, std::vector<uint8_t>& textData, std::string* error = nullptr);
bool WriteEditableText(const std::filesystem::path& outputPath, const std::vector<uint8_t>& textData,
                       std::string* error = nullptr);
bool ReadEditableText(const std::filesystem::path& inputPath, std::vector<uint8_t>& textData,
                      std::string* error = nullptr);
bool WriteEditableAnimation(const std::filesystem::path& outputPath, const std::vector<uint8_t>& animationData,
                            std::string* error = nullptr);
bool ReadEditableAnimation(const std::filesystem::path& inputPath, std::vector<uint8_t>& animationData,
                           std::string* error = nullptr);
bool WriteEditableSpriteFrames(const std::filesystem::path& outputPath, const std::vector<uint8_t>& frameData,
                               std::string* error = nullptr);
bool ReadEditableSpriteFrames(const std::filesystem::path& inputPath, std::vector<uint8_t>& frameData,
                              std::string* error = nullptr);

bool RuntimeAssetsNeedRebuild(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                              std::string* reason = nullptr);
bool BuildRuntimeAssets(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                        std::string* error = nullptr);
bool EnsureRuntimeAssetsBuilt(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                              std::string* reasonOrError = nullptr);

} // namespace PortAssetPipeline

#ifndef slic3r_GLTexture_hpp_
#define slic3r_GLTexture_hpp_

#include <atomic>
#include <string>
#include <vector>
#include <thread>

class wxImage;

namespace Slic3r {
namespace GUI {

    class GLTexture
    {
        class Compressor
        {
            struct Level
            {
                unsigned int w{ 0 };
                unsigned int h{ 0 };
                bool sent_to_gpu{ false };
                std::vector<unsigned char> src_data;
                std::vector<unsigned char> compressed_data;

                Level(unsigned int w, unsigned int h, const std::vector<unsigned char>& data) : w(w), h(h), sent_to_gpu(false), src_data(data) {}
            };

            GLTexture& m_texture;
            std::vector<Level> m_levels;
            std::thread m_thread;
            // Does the caller want the background thread to stop?
            // This atomic also works as a memory barrier for synchronizing the cancel event with the worker thread.
            std::atomic<bool> m_abort_compressing;
            // How many levels were compressed since the start of the background processing thread?
            // This atomic also works as a memory barrier for synchronizing results of the worker thread with the calling thread.
            std::atomic<unsigned int> m_num_levels_compressed;

        public:
            explicit Compressor(GLTexture& texture) : m_texture(texture), m_abort_compressing(false), m_num_levels_compressed(0) {}
            ~Compressor() { reset(); }

            void reset();

            void add_level(unsigned int w, unsigned int h, const std::vector<unsigned char>& data) { m_levels.emplace_back(w, h, data); }

            void start_compressing();

            bool unsent_compressed_data_available() const;
            void send_compressed_data_to_gpu();
            bool all_compressed_data_sent_to_gpu() const { return m_levels.empty(); }

        private:
            void compress();
        };

    public:
        enum class ECompressionType : unsigned char
        {
            None,
            SingleThreaded,
            MultiThreaded
        };

        struct UV
        {
            float u{ 0 };
            float v{ 0 };
        };

        struct Quad_UVs
        {
            UV left_bottom;
            UV right_bottom;
            UV right_top;
            UV left_top;
        };

        static Quad_UVs FullTextureUVs;

    protected:
        unsigned int m_id{ 0 };
        int m_width{ 0 };
        int m_height{ 0 };
        std::string m_source;
        Compressor m_compressor;

    public:
        GLTexture() : m_compressor(*this) {}
        virtual ~GLTexture() { reset(); }

        bool load_from_file(const std::string& filename, bool use_mipmaps, ECompressionType compression_type, bool apply_anisotropy);
        bool load_from_svg_file(const std::string& filename, bool use_mipmaps, bool compress, bool apply_anisotropy, unsigned int max_size_px);
        // meanings of states: (std::pair<int, bool>)
        // first field (int):
        // 0 -> no changes
        // 1 -> use white only color variant
        // 2 -> use gray only color variant
        // second field (bool):
        // false -> no changes
        // true -> add background color
        bool load_from_svg_files_as_sprites_array(const std::vector<std::string>& filenames, const std::vector<std::pair<int, bool>>& states, unsigned int sprite_size_px, bool compress);

        void reset();

        unsigned int get_id() const { return m_id; }
        int get_width() const { return m_width; }
        int get_height() const { return m_height; }

        const std::string& get_source() const { return m_source; }

        bool unsent_compressed_data_available() const { return m_compressor.unsent_compressed_data_available(); }
        void send_compressed_data_to_gpu() { m_compressor.send_compressed_data_to_gpu(); }
        bool all_compressed_data_sent_to_gpu() const { return m_compressor.all_compressed_data_sent_to_gpu(); }

        static void render_texture(unsigned int tex_id, float left, float right, float bottom, float top);
        static void render_sub_texture(unsigned int tex_id, float left, float right, float bottom, float top, const Quad_UVs& uvs);

#if ENABLE_TEXTURED_VOLUMES
    protected:
#else
    private:
#endif // ENABLE_TEXTURED_VOLUMES
        bool load_from_png(const std::string& filename, bool use_mipmaps, ECompressionType compression_type, bool apply_anisotropy);
#if ENABLE_TEXTURED_VOLUMES
        bool load_from_png_memory(const std::vector<unsigned char>& png_data, bool use_mipmaps, ECompressionType compression_type, bool apply_anisotropy);
#endif // ENABLE_TEXTURED_VOLUMES
        bool load_from_svg(const std::string& filename, bool use_mipmaps, bool compress, bool apply_anisotropy, unsigned int max_size_px);
        bool adjust_size_for_compression();
        void send_to_gpu(std::vector<unsigned char>& data, bool use_mipmaps, ECompressionType compression_type, bool apply_anisotropy,
            std::function<void(int, int, std::vector<unsigned char>&)> resampler);

#if ENABLE_TEXTURED_VOLUMES
        virtual void on_reset() {}
#endif // ENABLE_TEXTURED_VOLUMES

        friend class Compressor;
    };

#if ENABLE_TEXTURED_VOLUMES
    class GLIdeaMakerTexture : public GLTexture
    {
        enum class EWrapping
        {
            Repeat,
            Mirror,
            ClampToEdge,
            ClampToBorder
        };

        float m_repeat_x{ 1.0f };
        float m_repeat_y{ 1.0f };
        float m_rotation_z{ 0.0f };
        float m_translation_x{ 0.0f };
        float m_translation_y{ 0.0f };
        EWrapping m_wrapping{ EWrapping::Repeat };
        std::string m_imaker_id;
        std::string m_border_color;
        std::string m_version;

    public:
        bool load_from_ideamaker_texture_file(const std::string& filename, bool use_mipmaps, ECompressionType compression_type, bool apply_anisotropy);

    protected:
        virtual void on_reset() override {
            m_repeat_x = 1.0f;
            m_repeat_y = 1.0f;
            m_rotation_z = 0.0f;
            m_translation_x = 0.0f;
            m_translation_y = 0.0f;
            m_wrapping = EWrapping::Repeat;
            m_imaker_id.clear();
            m_border_color.clear();
            m_version.clear();
        }
    };
#endif // ENABLE_TEXTURED_VOLUMES

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLTexture_hpp_


#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <gst/gst.h>
#include <string>
#include <vector>
#include <format>

// liblo for OSC
#include <lo/lo.h>

//Eyepanels are 13 x 9, so let's just have 20x resolution for testing
#define WIDTH 260
#define HEIGHT 180
#define FPS 60

// Utility to read a file's contents into a std::string
std::string readFile(const std::string &filename) {
    std::ifstream file(filename);
    if (!file) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        exit(EXIT_FAILURE);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Vertex shader source (inline for simplicity)
const char* vertex_shader_src = R"(
    attribute vec2 position;
    void main() {
        gl_Position = vec4(position, 0.0, 1.0);
    }
)";

// Check for OpenGL errors
void check_gl_error(const char* msg) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::cerr << "OpenGL Error [" << msg << "]: 0x" << std::hex << err << std::endl;
    }
}

// Compile a shader
GLuint compile_shader(const char* src, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char info[512];
        glGetShaderInfoLog(shader, 512, NULL, info);
        std::cerr << "Shader Compilation Error: " << info << std::endl;
        exit(EXIT_FAILURE);
    }
    return shader;
}

// Global uniform locations
GLint liResolution, liTime,
        liPupil_L, liPupil_R, liPupil_S,
        liLidBtm_L, liLidBtm_R,
        liLidTop_L, liLidTop_R,
        liMood;

// OSC error callback
void osc_error_handler(int num, const char *msg, const char *path) {
    std::cerr << "OSC server error " << num << " in path "
              << (path ? path : "(null)") << ": " << msg << std::endl;
}

int osc_eyeL_Xcoord_handler(const char* path, const char* types,
                          lo_arg** argv, int argc,
                          lo_message msg,
                          void* user_data){
    if (argc >= 2) {
        //Patch the OSC incoming floats to shader variables
        glUniform2f(liPupil_L, argv[0]->f, 0);
        // Debug print
        std::cout << "EyeCoordL => X: " << argv[0]->f << std::endl;
    }
    return 0; // Return 0 on success
}

int osc_eyeR_coord_handler(const char* path, const char* types,
                          lo_arg** argv, int argc,
                          lo_message msg,
                          void* user_data){
    if (argc >= 2) {
        glUniform2f(liPupil_R, argv[0]->f, argv[1]->f);
        // Debug print
        std::cout << "EyeCoordR => X: " << argv[0]->f << ", Y: " << argv[1]->f << std::endl;
    }
    return 0; // Return 0 on success
}

// Set up OpenGL
GLuint setup_opengl() {
    // Load the fragment shader from file
    std::string fragSource = readFile("shader_eyes.glsl");

    // Compile shaders
    GLuint vertex_shader = compile_shader(vertex_shader_src, GL_VERTEX_SHADER);
    GLuint fragment_shader = compile_shader(fragSource.c_str(), GL_FRAGMENT_SHADER);

    // Link program
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char info[512];
        glGetProgramInfoLog(program, 512, NULL, info);
        std::cerr << "Program Linking Error: " << info << std::endl;
        exit(EXIT_FAILURE);
    }

    glUseProgram(program);

    // Get uniform locations
    /*
    iResolutionLoc = glGetUniformLocation(program, "iResolution");
    iMouseLoc      = glGetUniformLocation(program, "iMouse");
    iTimeLoc       = glGetUniformLocation(program, "iTime");
    */

    liResolution  = glGetUniformLocation(program, "iResolution");
    liTime        = glGetUniformLocation(program, "iTime");
    liPupil_L     = glGetUniformLocation(program, "iPupil_L");
    liPupil_R     = glGetUniformLocation(program, "iPupil_R");
    liPupil_S     = glGetUniformLocation(program, "iPupil_S");
    liLidBtm_L    = glGetUniformLocation(program, "iLidBtm_L");
    liLidBtm_R    = glGetUniformLocation(program, "iLidBtm_R");
    liLidTop_L = glGetUniformLocation(program, "iLidTop_L");
    liLidTop_R = glGetUniformLocation(program, "iLidTop_R");
    liMood        = glGetUniformLocation(program, "iMood");

    // Set up a full-screen quad
    GLfloat vertices[] = {
        -10.0f, 1.0f,
         0.0f,  -5.0f,
        10.0f,  1.0f
    };

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    GLint position_attrib = glGetAttribLocation(program, "position");
    glEnableVertexAttribArray(position_attrib);
    glVertexAttribPointer(position_attrib, 2, GL_FLOAT, GL_FALSE, 0, 0);

    return program;
}

int main() {
    //Set up OSC server (port 9000 or whichever VRChat/other client sends to)
    lo_server_thread osc_server = lo_server_thread_new("9000", osc_error_handler);
    if (!osc_server) {
        std::cerr << "Failed to create OSC server on port 9000.\n";
        return -1;
    }

    // Register a method that listens to /avatar/parameters/EyeCoord with 'ff' (2 floats)
    lo_server_thread_add_method(osc_server,
                                "/avatar/parameters/v2/EyeLeftX",
                                "f", // float
                                osc_eyeL_Xcoord_handler,
                                nullptr);

    // Start the OSC server listening thread
    lo_server_thread_start(osc_server);

    //Initialize EGL
    EGLDisplay egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(egl_display, NULL, NULL);

    // Choose EGL config
    EGLint config_attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig egl_config;
    EGLint num_configs;
    eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &num_configs);

    // Create PBuffer surface
    EGLint surface_attribs[] = {
        EGL_WIDTH, WIDTH,
        EGL_HEIGHT, HEIGHT,
        EGL_NONE
    };
    EGLSurface egl_surface = eglCreatePbufferSurface(egl_display, egl_config, surface_attribs);

    // Create EGL context
    EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);

    // Make context current
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

    // Set up OpenGL
    GLuint program = setup_opengl();

    // Set iResolution once, since width/height don't change
    glUniform2f(liResolution, float(WIDTH), float(HEIGHT));

    // Track start time for iTime
    double startTime = static_cast<double>(cv::getTickCount()) / cv::getTickFrequency();

    // Set up FBO
    GLuint fbo, texture;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer is not complete!" << std::endl;
        return -1;
    }

    // Initialize GStreamer
    gst_init(nullptr, nullptr);

    // GStreamer pipeline setup for uncompressed frames
    std::string pipeline = std::format(
        "appsrc is-live=true format=time do-timestamp=true "
        "caps=video/x-raw,format=BGR,width={},height={},framerate={}/1 "
        "! rtpvrawpay pt=96 mtu=1400 ! udpsink host=127.0.0.1 port=5000 sync=false",
        WIDTH, HEIGHT, FPS);
    cv::VideoWriter videoWriter;

    // Open GStreamer pipeline as a video writer
    if (!videoWriter.open(pipeline, 0, FPS, cv::Size(WIDTH, HEIGHT), true)) {
        std::cerr << "Failed to open GStreamer pipeline." << std::endl;
        return -1;
    }

    // Create an OpenCV window to preview frames
    cv::namedWindow("OpenCV Preview", cv::WINDOW_AUTOSIZE);

    // Render loop
    std::cout << "Streaming video to GStreamer pipeline..." << std::endl;
    while (true) {
        // Update uniforms
        double currentTime = static_cast<double>(cv::getTickCount()) / cv::getTickFrequency();
        float elapsed = static_cast<float>(currentTime - startTime);

        // iTime
        glUniform1f(liTime, elapsed);

        // Render
        glViewport(0, 0, WIDTH, HEIGHT);
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Read pixels
        std::vector<unsigned char> pixels(WIDTH * HEIGHT * 4);
        glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        // Convert to OpenCV Mat
        cv::Mat frame(HEIGHT, WIDTH, CV_8UC4, pixels.data());
        cv::cvtColor(frame, frame, cv::COLOR_RGBA2BGR);

        // Display preview
        cv::imshow("OpenCV Preview", frame);
        videoWriter.write(frame);

        // ~60 FPS
        if (cv::waitKey(14) == 27) {
            // Press ESC to exit
            break;
        }
    }

    // Cleanup
    videoWriter.release();
    glDeleteProgram(program);
    eglDestroySurface(egl_display, egl_surface);
    eglDestroyContext(egl_display, egl_context);
    eglTerminate(egl_display);

    // Stop OSC server
    lo_server_thread_stop(osc_server);
    lo_server_thread_free(osc_server);

    std::cout << "Streaming + OSC ended." << std::endl;
    return 0;
}

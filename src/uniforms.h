#pragma once

#include <map>
#include <queue>
#include <array>
#include <vector>
#include <string>
#include <functional>

#include "ada/gl/shader.h"
#include "ada/gl/texture.h"
#include "ada/gl/textureStream.h"
#include "ada/gl/fbo.h"
#include "ada/gl/pingpong.h"
#include "ada/gl/convolutionPyramid.h"

#include "ada/fs.h"
#include "ada/scene/camera.h"
#include "ada/scene/light.h"

#include "ada/gl/textureStreamAudio.h"
#include "types/files.h"
#include "tools/tracker.h"

typedef std::array<float, 4> UniformValue;

struct UniformData {
    std::string getType();

    void    set(const UniformValue &_value, size_t _size, bool _int);
    void    parse(const std::vector<std::string>& _command, size_t _start = 1);
    bool    check();

    std::queue<UniformValue>    queue;
    UniformValue                value;
    size_t                      size = 0;
    bool                        bInt = false;
    bool                        change = false;

};

typedef std::map<std::string, UniformData> UniformDataList;

struct UniformFunction {
    UniformFunction();
    UniformFunction(const std::string &_type);
    UniformFunction(const std::string &_type, std::function<void(ada::Shader&)> _assign);
    UniformFunction(const std::string &_type, std::function<void(ada::Shader&)> _assign, std::function<std::string()> _print);

    std::function<void(ada::Shader&)>   assign;
    std::function<std::string()>        print;
    std::string                         type;
    bool                                present = false;
};

typedef std::map<std::string, UniformFunction>      UniformFunctionsList;
typedef std::map<std::string, ada::Texture*>        TextureList;
typedef std::map<std::string, ada::TextureStream*>  StreamsList;

struct CameraData {
    glm::mat4   projection;
    glm::mat4   transform;
    // glm::mat3   rotation;
    // glm::vec3   translation;
};

class Uniforms {
public:
    Uniforms();
    virtual ~Uniforms();

    // Ingest new uniforms
    bool                    parseLine( const std::string &_line );

    bool                    addTexture( const std::string& _name, ada::Texture* _texture );
    bool                    addTexture( const std::string& _name, const std::string& _path, WatchFileList& _files, bool _flip = true, bool _verbose = true );
    bool                    addBumpTexture( const std::string& _name, const std::string& _path, WatchFileList& _files, bool _flip = true, bool _verbose = true );
    bool                    addStreamingTexture( const std::string& _name, const std::string& _url, bool _flip = true, bool _device = false, bool _verbose = true );
    bool                    addAudioTexture( const std::string& _name, const std::string& device_id, bool _flip = false, bool _verbose = true );
    bool                    addCameraTrack( const std::string& _name );

    void                    set( const std::string& _name, float _value);
    void                    set( const std::string& _name, float _x, float _y);
    void                    set( const std::string& _name, float _x, float _y, float _z);
    void                    set( const std::string& _name, float _x, float _y, float _z, float _w);
    
    void                    setCubeMap( ada::TextureCube* _cubemap );
    void                    setCubeMap( const std::string& _filename, WatchFileList& _files, bool _verbose = true);

    void                    setStreamPlay( const std::string& _name);
    void                    setStreamStop( const std::string& _name);
    void                    setStreamRestart( const std::string& _name );
    float                   getStreamTime( const std::string& _name);
    void                    setStreamTime( const std::string& _name, float _time );
    float                   getStreamPct( const std::string& _name);
    void                    setStreamPct( const std::string& _name, float _pct );
    float                   getStreamSpeed( const std::string& _name);
    void                    setStreamSpeed( const std::string& _name, float _speed );

    void                    setStreamsPlay();
    void                    setStreamsStop();
    void                    setStreamsRestart();
    
    void                    setStreamsPct( float _pct );
    void                    setStreamsTime( float _time );
    void                    setStreamsSpeed( float _speed );
    void                    setStreamsPrevs( size_t _total );

    void                    updateStreams(size_t _frame);

    // Check presence of uniforms on shaders
    void                    checkPresenceIn( const std::string &_vert_src, const std::string &_frag_src );

    // Feed uniforms to a specific shader
    bool                    feedTo( ada::Shader &_shader, bool _lights = true, bool _buffers = true);
    bool                    feedTo( ada::Shader *_shader, bool _lights = true, bool _buffers = true);

    ada::Camera&            getCamera() { return cameras[0]; }

    // Debug
    void                    printAvailableUniforms(bool _non_active);
    void                    printDefinedUniforms(bool _csv = false);
    void                    printBuffers();
    void                    printTextures();
    void                    printStreams();
    void                    printLights();

    // Change state
    void                    flagChange();
    void                    unflagChange();
    bool                    haveChange();

    void                    clear();

    // Manually defined uniforms (through console IN)
    UniformDataList         data;

    // Automatic uniforms
    UniformFunctionsList    functions;

    // Common 
    TextureList             textures;
    StreamsList             streams;
    ada::TextureCube*       cubemap;

    // Buffers
    std::vector<ada::Fbo>                   buffers;
    std::vector<ada::PingPong>              doubleBuffers;
    std::vector<ada::ConvolutionPyramid>    convolution_pyramids;

    // 3D Scene Uniforms 
    std::vector<ada::Camera>    cameras;
    std::vector<CameraData>     cameraTrack;

    std::vector<ada::Light>     lights;

    // Tracker
    Tracker                     tracker;

protected:
    size_t                  m_streamsPrevs;
    bool                    m_streamsPrevsChange;
    bool                    m_change;
    bool                    m_is_audio_init;
};



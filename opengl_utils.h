#define TWGL_log_error(...) printf(__VA_ARGS__)

GLuint CompileShaderFromSource(GLenum shaderType, const char *source){
  GLuint shader = glCreateShader(shaderType);
  glShaderSource(shader, 1, &source, 0);
  glCompileShader(shader);
  GLint compileStatus = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
  if(compileStatus == GL_FALSE){
    GLint infoLogLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
    char infoLog[infoLogLength];
    glGetShaderInfoLog(shader, infoLogLength, &infoLogLength, infoLog);
    TWGL_log_error("%s", infoLog);
    glDeleteShader(shader);
    shader = 0;
  }
  return shader;
}

int CreateAndLinkShaderProgramFromSources(const char *vertexShaderSource, const char *fragmentShaderSource){
  static const GLenum shaderTypes[] {
    GL_VERTEX_SHADER,
    GL_FRAGMENT_SHADER,
  };

  auto vertexShader = CompileShaderFromSource(vertexShaderSource);
  auto fragmentShader = CompileShaderFromSource(fragmentShaderSource);
  if(fragmentShader == 0 && vertexShader != 0) {
    glDeleteShader(vertexShader);
    return 0;
  }


  glCreateShader(GL_VERTEX_SHADER);
}
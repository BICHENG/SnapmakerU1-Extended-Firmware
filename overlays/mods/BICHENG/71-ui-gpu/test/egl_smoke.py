"""One offscreen GLES draw; run manually as root on an idle U1."""
import ctypes as C
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import time


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def render():
    # Reuse the original device probe's ctypes EGL path; no GUI or framebuffer writes.
    lib = C.CDLL('/usr/lib/libmali.so.1', mode=C.RTLD_GLOBAL)
    P, I, U, F = C.c_void_p, C.c_int, C.c_uint, C.c_float

    def fn(name, result, args):
        function = getattr(lib, name)
        function.restype, function.argtypes = result, args
        return function

    fd = os.open('/dev/dri/card0', os.O_RDWR)
    gbm = display = context = None
    initialized = False
    try:
        gbm = fn('gbm_create_device', P, [I])(fd)
        require(gbm, 'gbm_create_device failed')
        display = fn('eglGetDisplay', P, [P])(gbm)
        major, minor = I(), I()
        require(fn('eglInitialize', U, [P, C.POINTER(I), C.POINTER(I)])(display, C.byref(major), C.byref(minor)), 'eglInitialize failed')
        initialized = True
        require(fn('eglBindAPI', U, [U])(0x30A0), 'eglBindAPI failed')
        config, count = P(), I()
        attrs = (I * 5)(0x3040, 0x40, 0x3033, 0, 0x3038)
        require(fn('eglChooseConfig', U, [P, C.POINTER(I), C.POINTER(P), I, C.POINTER(I)])(display, attrs, C.byref(config), 1, C.byref(count)) and count.value, 'No GLES 3 config')
        context = fn('eglCreateContext', P, [P, P, P, C.POINTER(I)])(display, config, None, (I * 3)(0x3098, 3, 0x3038))
        require(context, 'eglCreateContext failed')
        require(fn('eglMakeCurrent', U, [P, P, P, P])(display, None, None, context), 'eglMakeCurrent failed')
        get_string = fn('glGetString', C.c_char_p, [U])
        result = {'egl': f'{major.value}.{minor.value}', 'renderer': get_string(0x1F01).decode(),
                  'version': get_string(0x1F02).decode()}
        require(result['renderer'].startswith('Mali-G52'), result)

        texture, fbo = U(), U()
        fn('glGenTextures', None, [I, C.POINTER(U)])(1, C.byref(texture))
        fn('glBindTexture', None, [U, U])(0xDE1, texture)
        fn('glTexImage2D', None, [U, I, I, I, I, I, U, U, P])(0xDE1, 0, 0x1908, 16, 16, 0, 0x1908, 0x1401, None)
        fn('glGenFramebuffers', None, [I, C.POINTER(U)])(1, C.byref(fbo))
        fn('glBindFramebuffer', None, [U, U])(0x8D40, fbo)
        fn('glFramebufferTexture2D', None, [U, U, U, U, I])(0x8D40, 0x8CE0, 0xDE1, texture, 0)
        require(fn('glCheckFramebufferStatus', U, [U])(0x8D40) == 0x8CD5, 'Incomplete framebuffer')

        def shader(kind, source):
            obj = fn('glCreateShader', U, [U])(kind)
            text = C.c_char_p(source)
            fn('glShaderSource', None, [U, I, C.POINTER(C.c_char_p), P])(obj, 1, C.byref(text), None)
            fn('glCompileShader', None, [U])(obj)
            ok = I()
            fn('glGetShaderiv', None, [U, U, C.POINTER(I)])(obj, 0x8B81, C.byref(ok))
            require(ok.value, 'Shader compilation failed')
            return obj

        vertex = shader(0x8B31, b'#version 300 es\nlayout(location=0) in vec2 position; void main(){gl_Position=vec4(position,0.0,1.0);}')
        fragment = shader(0x8B30, b'#version 300 es\nprecision mediump float; out vec4 color; void main(){color=vec4(0.2,0.7,0.3,1.0);}')
        program = fn('glCreateProgram', U, [])()
        for obj in (vertex, fragment):
            fn('glAttachShader', None, [U, U])(program, obj)
        fn('glLinkProgram', None, [U])(program)
        ok = I()
        fn('glGetProgramiv', None, [U, U, C.POINTER(I)])(program, 0x8B82, C.byref(ok))
        require(ok.value, 'Program link failed')
        fn('glUseProgram', None, [U])(program)
        buffer = U()
        fn('glGenBuffers', None, [I, C.POINTER(U)])(1, C.byref(buffer))
        fn('glBindBuffer', None, [U, U])(0x8892, buffer)
        vertices = (F * 6)(-1, -1, 1, -1, 0, 1)
        fn('glBufferData', None, [U, C.c_ssize_t, P, U])(0x8892, C.sizeof(vertices), vertices, 0x88E4)
        fn('glVertexAttribPointer', None, [U, I, U, U, I, P])(0, 2, 0x1406, 0, 0, None)
        fn('glEnableVertexAttribArray', None, [U])(0)
        fn('glViewport', None, [I, I, I, I])(0, 0, 16, 16)
        fn('glClearColor', None, [F, F, F, F])(0, 0, 0, 1)
        fn('glClear', None, [U])(0x4000)
        fn('glDrawArrays', None, [U, I, I])(4, 0, 3)
        pixels = (C.c_ubyte * (16 * 16 * 4))()
        fn('glReadPixels', None, [I, I, I, I, U, U, P])(0, 0, 16, 16, 0x1908, 0x1401, pixels)
        result['center_rgba'] = list(pixels[(8 * 16 + 8) * 4:(8 * 16 + 8) * 4 + 4])
        result['corner_rgba'] = list(pixels[(15 * 16) * 4:(15 * 16) * 4 + 4])
        result['gl_error'] = fn('glGetError', U, [])()
        require(all(abs(a - b) <= 1 for a, b in zip(result['center_rgba'], (51, 179, 77, 255))), result)
        require(result['corner_rgba'] == [0, 0, 0, 255] and result['gl_error'] == 0, result)
    finally:
        cleaned = True
        if initialized:
            cleaned &= bool(fn('eglMakeCurrent', U, [P, P, P, P])(display, None, None, None))
            if context:
                # Destroying the context also frees its textures, buffers, shaders and FBO.
                cleaned &= bool(fn('eglDestroyContext', U, [P, P])(display, context))
            cleaned &= bool(fn('eglTerminate', U, [P])(display))
            cleaned &= bool(fn('eglReleaseThread', U, [])())
        if gbm:
            fn('gbm_device_destroy', None, [P])(gbm)
        os.close(fd)
        require(cleaned, 'EGL cleanup failed')
    result['egl_cleanup'] = True
    return result


def gpu_clients():
    device = Path('/dev/mali0').stat().st_rdev
    clients = []
    for process in Path('/proc').iterdir():
        if not process.name.isdigit():
            continue
        try:
            for fd in (process / 'fd').iterdir():
                try:
                    info = fd.stat()
                except FileNotFoundError:
                    continue
                if stat.S_ISCHR(info.st_mode) and info.st_rdev == device:
                    clients.append(int(process.name))
                    break
        except (FileNotFoundError, ProcessLookupError):
            continue
    return clients


def main():
    require(os.geteuid() == 0, 'Run as root to inspect other GPU clients')
    limit = Path('/sys/class/devfreq/ff320000.gpu/max_freq')
    require(limit.read_text().strip() == '400000000', 'GPU frequency limit is not 400 MHz')
    before = gpu_clients()
    child = subprocess.run([sys.executable, str(Path(__file__).resolve()), '--render'],
                           capture_output=True, text=True, timeout=20)
    if child.returncode:
        raise RuntimeError(child.stdout + child.stderr)
    result = json.loads(next(line.removeprefix('GPU_SMOKE=') for line in child.stdout.splitlines()
                             if line.startswith('GPU_SMOKE=')))
    runtime = Path('/sys/class/devfreq/ff320000.gpu/device/power/runtime_status')
    deadline = time.monotonic() + 5
    while True:
        clients = sorted(set(before + gpu_clients()))
        result['runtime_status'] = runtime.read_text().strip()
        if clients:
            result.update(suspend_check='not_isolated', other_gpu_clients=clients)
            break
        if result['runtime_status'] == 'suspended':
            result['suspend_check'] = 'passed'
            break
        if time.monotonic() >= deadline:
            raise RuntimeError('GPU did not suspend within 5 seconds after the isolated probe exited')
        time.sleep(0.1)
    print(json.dumps(result))


if __name__ == '__main__':
    if sys.argv[1:] == ['--render']:
        print('GPU_SMOKE=' + json.dumps(render()), flush=True)
    elif not sys.argv[1:]:
        main()
    else:
        raise SystemExit('Usage: python3 egl_smoke.py')

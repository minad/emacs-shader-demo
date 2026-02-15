;;; ob-shader.el --- Org babel test -*- lexical-binding: t -*-
;;; Commentary:
;;; Code:

(require 'ob)

(defvar ob-shader-start (float-time))
(defvar ob-shader-canvas '(image :type canvas :canvas-width 300 :canvas-height 300 :canvas-id ob-canvas))
(defvar ob-shader-timer (run-with-timer
                         nil (/ 1 60.0)
                         (lambda ()
                           (ob-shader-render ob-shader-canvas (- (float-time) ob-shader-start)))))
(setq backup-inhibited t
      create-lockfiles nil
      auto-save-default nil
      auto-save-no-message t
      auto-save-list-file-prefix nil
      org-confirm-babel-evaluate nil)

(defun org-babel-execute:shader (body _params)
  (ob-shader-load (format "
precision mediump float;
varying vec2 vUV;
uniform float t;
void main() {
  vec2 p = vUV * 2.0 - 1.0;
  float x = p.x, y = p.y, a = atan(y, x), r = length(p);
  float R, G, B;
  %s
  gl_FragColor = vec4(B, G, R, 1.0);
}
" body)))

(font-lock-add-keywords 'org-mode `((": {SHADER}" 0 '(face nil display ,ob-shader-canvas))) t)
(shell-command (format "gcc -O2 -I%ssrc ob-shader.c -o /tmp/ob-shader.so -lGL -lEGL -fPIC -shared"
                       (shell-quote-argument source-directory)))
(module-load "/tmp/ob-shader.so")

(add-hook 'org-mode-hook
          (lambda ()
            (when (string-search "ob-shader.org" (buffer-file-name))
              (with-silent-modifications
                (org-babel-execute-buffer)))))

(provide 'ob-shader)
;;; ob-shader.el ends here

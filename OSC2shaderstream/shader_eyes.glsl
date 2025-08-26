
precision mediump float;

uniform vec2 iResolution;
uniform float iTime;

uniform vec2 iPupil_L;
uniform vec2 iPupil_R;
uniform float iPupil_S;
uniform float iLidBtm_L;
uniform float iLidBtm_R;
uniform float iLidTop_L;
uniform float iLidTop_R;
uniform float iMood;

//#define PIXELIZE

float sdEyeShape(vec2 p, float r, float m){
    const float k = 1.0-sqrt(2.0);

    p = abs(p) - vec2(0.45+0.7*r,0.2+0.4*r); //r = pupil "radius"

    p = (p.y>p.x) ? p.yx : p.xy;
    p.y += 0.3333;

    if( p.y<0.0 && p.y+p.x*k<0.0 ) return p.x;
    if( p.x<p.y ) return (p.x+p.y)*sqrt(0.5);

    return length(p);
}

float sdBox( in vec2 p, in vec2 b ){
    vec2 d = abs(p)-b;
    return length(max(d,0.0)) + min(max(d.x,d.y),0.0);
}

float sdTopLid( in vec2 p, in float m, in float lr){
    p -= vec2(lr*0.25+m*m*lr,0.0);
    float h = -dot(p, vec2(-lr*m*0.75, -1.0));
    return max(length(p)-2.0,h);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord ) {
    /*float trad = 0.5 + 0.5* sin(3.0*iTime);
    vec2 iPupil_L = vec2(sin(iTime*4.0)*trad, cos(iTime*5.1)*trad);
    vec2 iPupil_R = vec2(sin(iTime*4.0)*trad, cos(iTime*5.1)*trad);
    float iLidBtm_L = 0.5 + 0.5*sin(iTime*0.5);
    float iLidBtm_R = iLidBtm_L;
    float iLidTop_L = 0.5 + 0.5*sin(iTime*2.5);
    float iLidTop_R = iLidTop_L;
    float iMood = sin(iTime*0.15);
    float iPupil_S = 0.5; //0.5+0.5*sin(iTime*4.15);
    */

    float pLineThicc = 0.2222;
    vec2 uv = (2.0*fragCoord-iResolution.xy)/iResolution.y;

    #ifdef PIXELIZE
    float pixelSize = 0.08;
    float pixelSpacing = 0.004;
    // Quantize UV
    uv = floor(uv / pixelSize) * pixelSize + pixelSize * 0.5;
    // Grid
    vec2 localPos = mod(uv, pixelSize);
    if (localPos.x < pixelSpacing || localPos.x > pixelSize - pixelSpacing ||
        localPos.y < pixelSpacing || localPos.y > pixelSize - pixelSpacing) {
        fragColor = vec4(0.0); // Set fragment to background color and exit
        return;
        }
        #endif

        // box params
        float ratio = iResolution.x/iResolution.y;

    float xl = uv.x*2.0+ratio;
    float xr = uv.x*2.0-ratio;

    //EYEGRAPHICS SDF
    float d = min(sdEyeShape(vec2(xl, uv.y)-iPupil_L, iPupil_S, iMood),
                  sdEyeShape(vec2(xr, uv.y)-iPupil_R, iPupil_S, -iMood));

    d = max(d, min(sdTopLid(vec2(xl+0.5*ratio, uv.y-iLidTop_L+iLidBtm_L*0.666)-iPupil_L, iMood*iLidTop_L, 1.0),
                   sdTopLid(vec2(xr-0.5*ratio, uv.y-iLidTop_R+iLidBtm_R*0.666)-iPupil_R, iMood*iLidTop_R, -1.0)));

    d = max(d, -min(length(vec2(xl+0.75*ratio,uv.y+20.0-(4.120-iLidBtm_L+0.9*iPupil_R.y)))-16.0,
                    length(vec2(xr-0.75*ratio,uv.y+20.0-(4.120-iLidBtm_R+0.9*iPupil_L.y)))-16.0));

    d = max(d, sdBox(uv-vec2(0.0,0.0),vec2(ratio,1.0)));


    // colorize
    //vec3 col = (d>0.0) ? vec3(0.621,0.420,0.69) : vec3(0.0,0.0,0.0);
    // Time varying pixel color
    //col += 0.5 + 0.5*cos(iTime+uv.xyy+vec3(1,4,2));
    //col *= 1.0 - exp2(-20.0*abs(d));
    //col *= 0.8 + 0.2*cos(20.0*abs(d));
    //col = mix( col, vec3(1.0), 1.0-smoothstep(0.0,0.01,abs(d)) );

    // default purple
    vec3 col = vec3(0.9,0.78,1.75)*1.4;
    //fragment = vec4( r,r,r, 1.0 );

    // Output to screen
    fragColor = vec4(mix(vec3(0.0), col, smoothstep(-pLineThicc, 0.0, -abs(d))), 1.0);
    //fragColor = vec4(mix(vec3(0.0), col, 1.0), 1.0);

}

void main() {
    mainImage(gl_FragColor, gl_FragCoord.xy);
}

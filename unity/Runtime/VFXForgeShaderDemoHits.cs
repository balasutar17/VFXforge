// VFX Forge: sends a test impact to a Shader Animator every few seconds, so a shield can be
// seen reacting without writing any code.
using UnityEngine;

namespace VFXForge
{
    [AddComponentMenu("VFX Forge/Shader Demo Hits")]
    [RequireComponent(typeof(VFXForgeShaderAnimator))]
    public class VFXForgeShaderDemoHits : MonoBehaviour
    {
        public float every = 2.5f;
        public float radius = 0.5f;
        VFXForgeShaderAnimator animator;
        float next;

        void Start() { animator = GetComponent<VFXForgeShaderAnimator>(); next = Time.time + 0.5f; }

        void Update()
        {
            if (Time.time < next) return;
            next = Time.time + every;
            Vector3 dir = Random.onUnitSphere;
            animator.Hit(transform.position + dir * 0.5f * transform.lossyScale.x, radius);
        }
    }
}

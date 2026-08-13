void InstallRawPatch(auto* context)
{
    constexpr auto* ManifestId = PSH_MANIFEST_SITE("fixture.items.rawWrite");
    (void)ManifestId;
    const unsigned char expected[]{0x90};
    const unsigned char replacement[]{0xCC};
    context->PatchBytes(0x1000, expected, 1, replacement, 1);
}

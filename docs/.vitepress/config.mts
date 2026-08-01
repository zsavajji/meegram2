import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'MeeGram',
  description: 'Telegram for the Nokia N9 — build and architecture documentation',
  lang: 'en-GB',
  cleanUrls: true,
  lastUpdated: true,

  // Built into docs/.vitepress/dist and served from a project subpath: GitHub Pages
  // puts a project site at <user>.github.io/<repo>/, and the repo is meegram2 under
  // either owner. Set this back to '/' for a user/org site or a custom domain at the
  // root - a wrong base gives a page that loads and then 404s every asset on it.
  base: '/meegram2/',

  themeConfig: {
    nav: [
      { text: 'Features', link: '/features' },
      { text: 'Architecture', link: '/architecture' },
      { text: 'Restructuring', link: '/restructuring' },
      { text: 'Building', link: '/building' },
      { text: 'Troubleshooting', link: '/troubleshooting' },
      {
        text: 'v0.3.0',
        items: [
          { text: 'Changelog', link: 'https://github.com/qtinsider/meegram2/blob/main/debian/changelog' },
          { text: 'Issues', link: 'https://github.com/qtinsider/meegram2/issues' },
        ],
      },
    ],

    sidebar: [
      {
        text: 'Overview',
        items: [{ text: 'What MeeGram is', link: '/' }],
      },
      {
        text: 'Features',
        collapsed: false,
        items: [
          { text: 'At a glance', link: '/features#at-a-glance' },
          { text: 'Chat list', link: '/features#chat-list' },
          { text: 'Messages', link: '/features#messages' },
          { text: 'Media', link: '/features#media' },
          { text: 'Profile', link: '/features#profile' },
          { text: 'Presence', link: '/features#presence' },
          { text: 'Notifications', link: '/features#notifications' },
          { text: 'Not implemented', link: '/features#not-implemented' },
          { text: 'Performance work', link: '/features#performance-work' },
        ],
      },
      {
        text: 'Architecture',
        collapsed: false,
        items: [
          { text: 'The shape of it', link: '/architecture#the-shape-of-it' },
          { text: 'Ownership', link: '/architecture#ownership' },
          { text: 'Data flow', link: '/architecture#data-flow-one-incoming-message' },
          { text: 'Data flow: notifications', link: '/architecture#data-flow-one-notification' },
          { text: 'StorageManager', link: '/architecture#storagemanager-the-hub' },
          { text: 'C++/QML boundary', link: '/architecture#the-c-qml-boundary' },
          { text: 'Ids across the boundary', link: '/architecture#ids-cross-the-boundary-as-decimal-strings-in-both-directions' },
          { text: 'Threading', link: '/architecture#threading' },
          { text: 'Client: the only door', link: '/architecture#client-is-the-only-door-to-tdlib' },
          { text: 'Component reference', link: '/architecture#component-reference' },
          { text: 'What the graph showed', link: '/architecture#what-the-knowledge-graph-showed' },
        ],
      },
      {
        text: 'Restructuring',
        collapsed: true,
        items: [
          { text: 'What the socket costs', link: '/restructuring#what-the-socket-actually-costs' },
          { text: 'Where the split goes', link: '/restructuring#why-the-split-goes-below-storagemanager-not-around-it' },
          { text: 'The wire format', link: '/restructuring#the-hard-part-the-wire-format' },
          { text: 'What relocates', link: '/restructuring#what-genuinely-relocates' },
          { text: 'Sequencing', link: '/restructuring#sequencing' },
          { text: 'The cheap alternative', link: '/restructuring#the-cheap-alternative-for-comparison' },
          { text: 'Open questions', link: '/restructuring#open-questions' },
        ],
      },
      {
        text: 'Building',
        collapsed: false,
        items: [
          { text: 'Start to finish', link: '/building#the-whole-thing-start-to-finish' },
          { text: 'The blocker', link: '/building#the-one-thing-that-will-block-you' },
          { text: '1 · Host packages', link: '/building#_1-host-packages' },
          { text: '2 · Submodules', link: '/building#_2-submodules' },
          { text: '3 · Cross-toolchain', link: '/building#_3-cross-toolchain' },
          { text: '4 · Dependencies', link: '/building#_4-dependencies' },
          { text: '5 · Configure and build', link: '/building#_5-configure-and-build' },
          { text: '6 · Package', link: '/building#_6-package' },
        ],
      },
      {
        text: 'Troubleshooting',
        collapsed: false,
        items: [
          { text: 'Build errors', link: '/troubleshooting#build-errors' },
          { text: 'Known rough edges', link: '/troubleshooting#known-rough-edges' },
          { text: 'Verification status', link: '/troubleshooting#verification-status' },
        ],
      },
    ],

    socialLinks: [{ icon: 'github', link: 'https://github.com/qtinsider/meegram2' }],

    search: { provider: 'local' },

    outline: { level: [2, 3], label: 'On this page' },

    editLink: {
      pattern: 'https://github.com/qtinsider/meegram2/edit/main/docs/:path',
      text: 'Edit this page on GitHub',
    },

    footer: {
      message: 'Unofficial Telegram client for MeeGo 1.2 Harmattan.',
      copyright: 'Built with VitePress',
    },
  },
})

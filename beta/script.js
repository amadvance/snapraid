document.addEventListener('DOMContentLoaded', () => {
    // Generate TOC for FAQ page
    const faqTocList = document.getElementById('faq-toc-list');
    const faqContent = document.getElementById('faq-content');

    if (faqTocList && faqContent) {
        const sections = faqContent.querySelectorAll('.faq-section');
        sections.forEach(section => {
            const h2 = section.querySelector('h2');
            if (h2) {
                 // Create H2 Link
                 const li = document.createElement('li');
                 const a = document.createElement('a');
                 a.href = '#' + h2.id;
                 a.textContent = h2.textContent;
                 li.appendChild(a);
                 faqTocList.appendChild(li);

                 // Create H3 Links (Questions)
                 const questions = section.querySelectorAll('.faq-question');
                 if (questions.length > 0) {
                     const subUl = document.createElement('ul');
                     questions.forEach(q => {
                         const subLi = document.createElement('li');
                         const subA = document.createElement('a');
                         subA.href = '#' + q.id;
                         subA.textContent = q.textContent;
                         subLi.appendChild(subA);
                         subUl.appendChild(subLi);
                     });
                     li.appendChild(subUl);
                 }
            }
        });
    }

    // Generate TOC for Manual page
    const manualTocList = document.getElementById('manual-toc-list');
    const manualContainer = document.getElementById('manual-container');

    if (manualTocList && manualContainer) {
        // Query specific manual title classes
        const headers = manualContainer.querySelectorAll('.manual-title-level-1, .manual-title-level-2, .manual-title-level-3');
        
        let currentLevel1 = null;
        let currentLevel2Ul = null;
        
        headers.forEach(header => {
            // Determine level based on class name
            let level = 1;
            if (header.classList.contains('manual-title-level-2')) level = 2;
            if (header.classList.contains('manual-title-level-3')) level = 3;

            if (level === 1) {
                const li = document.createElement('li');
                const a = document.createElement('a');
                a.href = '#' + header.id;
                a.textContent = header.textContent;
                li.appendChild(a);
                manualTocList.appendChild(li);
                
                currentLevel1 = li;
                currentLevel2Ul = null;
            } 
            else if (level === 2 && currentLevel1) {
                if (!currentLevel2Ul) {
                    currentLevel2Ul = document.createElement('ul');
                    currentLevel1.appendChild(currentLevel2Ul);
                }
                const li = document.createElement('li');
                const a = document.createElement('a');
                a.href = '#' + header.id;
                a.textContent = header.textContent;
                li.appendChild(a);
                currentLevel2Ul.appendChild(li);
            }
            // Add level 3 handling if needed later
        });
    }

    // Mobile menu closes when a link is clicked
    const links = document.querySelectorAll('nav a');
    const checkbox = document.getElementById('menu-toggle');
    
    links.forEach(link => {
        link.addEventListener('click', () => {
            if (checkbox) checkbox.checked = false;
        });
    });

    // Simple smooth scroll for anchor links
    document.querySelectorAll('a[href^="#"]').forEach(anchor => {
        anchor.addEventListener('click', function (e) {
            e.preventDefault();
            const target = document.querySelector(this.getAttribute('href'));
            if (target) {
                target.scrollIntoView({
                    behavior: 'smooth'
                });
            }
        });
    });

    // FAQ Search functionality for .faq-item structure
    const faqSearch = document.getElementById('faq-search');
    if (faqSearch) {
        faqSearch.addEventListener('input', (e) => {
            const term = e.target.value.toLowerCase();
            const container = document.getElementById('faq-content');
            const tocList = document.getElementById('faq-toc-list');

            if (!container) return;

            const items = container.querySelectorAll('.faq-item');
            
            items.forEach(item => {
                const questionHeader = item.querySelector('.faq-question');
                const question = questionHeader.textContent.toLowerCase();
                const answer = item.querySelector('.faq-answer').textContent.toLowerCase();
                
                const isMatch = question.includes(term) || answer.includes(term);
                
                // Toggle display of the entire item in main content
                item.style.display = isMatch ? '' : 'none';

                // Toggle display of TOC item corresponding to this question
                if (tocList && questionHeader.id) {
                    const tocLink = tocList.querySelector(`a[href="#${CSS.escape(questionHeader.id)}"]`);
                    if (tocLink) {
                        const tocItem = tocLink.parentElement;
                        tocItem.style.display = isMatch ? '' : 'none';
                    }
                }
            });
            
            // Auto-hide sections if all their items are hidden
            const sections = container.querySelectorAll('.faq-section');
            sections.forEach(section => {
                const visibleItems = section.querySelectorAll('.faq-item:not([style*="display: none"])');
                const isSectionVisible = visibleItems.length > 0;
                
                section.style.display = isSectionVisible ? '' : 'none';

                // Toggle display of TOC section
                const sectionHeader = section.querySelector('h2');
                if (tocList && sectionHeader && sectionHeader.id) {
                     const tocLink = tocList.querySelector(`a[href="#${CSS.escape(sectionHeader.id)}"]`);
                     if (tocLink) {
                         // The section link is directly inside the top-level li of the TOC
                         const tocSectionItem = tocLink.parentElement;
                         tocSectionItem.style.display = isSectionVisible ? '' : 'none';
                     }
                }
            });
        });
    }

    // Language Selector functionality
    const languageSelector = document.getElementById('language-selector');
    if (languageSelector) {
        // Automatically select the option matching the current file name
        const currentFile = window.location.pathname.split('/').pop() || 'manual.html';
        const options = Array.from(languageSelector.options);
        const matchingOption = options.find(opt => opt.value === currentFile);
        if (matchingOption) {
            languageSelector.value = currentFile;
        }

        // Redirect on change
        languageSelector.addEventListener('change', (e) => {
            const selectedPage = e.target.value;
            if (selectedPage) {
                window.location.href = selectedPage;
            }
        });
    }

    // UI Gallery Preview on Hover
    const shots = document.querySelectorAll('.ui-shot');
    const overlay = document.getElementById('ui-preview-overlay');
    const previewImg = document.getElementById('ui-preview-img');
    
    if (overlay && previewImg) {
        shots.forEach(shot => {
            shot.addEventListener('mouseenter', () => {
                const largeSrc = shot.getAttribute('data-large');
                if (largeSrc) {
                    previewImg.src = largeSrc;
                    overlay.classList.add('active');
                }
            });
            
            shot.addEventListener('mouseleave', () => {
                overlay.classList.remove('active');
            });
        });
    }

    // Global Popover Logic for Compare Table
    const infoBtns = document.querySelectorAll('.info-btn');
    const globalPopover = document.getElementById('global-popover');
    const globalPopoverContent = document.getElementById('global-popover-content');
    const popoverOverlay = document.getElementById('popover-overlay');

    if (infoBtns.length > 0 && globalPopover && popoverOverlay) {
        
        function closePopover() {
            globalPopover.classList.remove('active');
            popoverOverlay.classList.remove('active');
            globalPopover.style.opacity = ''; // reset just in case
        }

        popoverOverlay.addEventListener('click', closePopover);

        infoBtns.forEach(btn => {
            btn.addEventListener('click', (e) => {
                e.stopPropagation();
                e.preventDefault();

                // Get content from the hidden sibling div
                const contentDiv = btn.parentElement.querySelector('.info-content');
                if (!contentDiv) return;

                globalPopoverContent.innerHTML = contentDiv.innerHTML;
                
                // Measure the popover by making it transparent but blocked
                globalPopover.style.opacity = '0';
                globalPopover.classList.add('active');
                
                const popWidth = globalPopover.offsetWidth;
                const popHeight = globalPopover.offsetHeight;

                globalPopover.classList.remove('active');
                globalPopover.style.opacity = '';

                // Position the popover
                const rect = btn.getBoundingClientRect();
                const scrollX = window.pageXOffset || document.documentElement.scrollLeft;
                const scrollY = window.pageYOffset || document.documentElement.scrollTop;

                // Calculate position (centered above the button by default)
                let top = rect.top + scrollY - popHeight - 15;
                let left = rect.left + scrollX - (popWidth / 2) + (rect.width / 2);

                 // Simple collision detection
                if (rect.top - popHeight - 20 < 0) {
                    // flip to bottom
                    top = rect.bottom + scrollY + 15;
                }
                
                // ensure left is on screen
                if (left < 10) left = 10;
                if (left + popWidth > window.innerWidth) {
                    left = window.innerWidth - popWidth - 10;
                }

                globalPopover.style.top = `${top}px`;
                globalPopover.style.left = `${left}px`;

                globalPopover.classList.add('active');
                popoverOverlay.classList.add('active');
            });
        });

        // Close on resize to prevent weird positioning
        window.addEventListener('resize', closePopover);
    }
});
